/*                      All the necessary headers are added in ksocket.h, and only ksocket.h is included here.

*/

#include "ksocket.h"

// Define the maximum payload size based on the 4-byte header.
#define PAYLOAD_SIZE (MESSAGE_SIZE - sizeof(FrameHeader))

// Internal frame types
typedef enum
{
    FRAME_DATA = 0,
    FRAME_ACK = 1,
    FRAME_EOF = 2,
    FRAME_EOF_ACK = 3,
    FRAME_FIN = 4,
    FRAME_FIN_ACK = 5
} FrameType;

// Frame header structure used internally for framing data.
#pragma pack(push, 1)
typedef struct
{
    uint8_t type;    // Data, ACK or control frame type
    uint8_t seq;     // Sequence number (for data and ACK frames)
    uint16_t length; // Payload length in bytes
} FrameHeader;
#pragma pack(pop)

// Global KSocket table. For simplicity we use the UDP socket descriptor as the index.
KSocket ksocket_table[MAX_KTP_SOCKETS];

// Simulate an unreliable link by randomly dropping data frames.
int dropMessage(float p)
{
    float random = (float)rand() / RAND_MAX;
    return (random < p) ? 1 : 0;
}

// Create a KTP socket (a thin wrapper over a UDP socket).
int k_socket(int domain, int type, int protocol)
{
    if (type != SOCK_KTP)
    {
        errno = EINVAL;
        return -1;
    }
    int udp_sock = socket(domain, SOCK_DGRAM, protocol);
    if (udp_sock < 0)
    {
        perror("Failed to create UDP socket");
        return -1;
    }
    if (udp_sock < MAX_KTP_SOCKETS)
    {
        ksocket_table[udp_sock].next_seq = 0;
        ksocket_table[udp_sock].is_bound = 0;
    }
    return udp_sock;
}

// Bind the KTP socket to a given source address and port and record destination details.
int k_bind(int sockfd, const char *src_ip, uint16_t src_port, const char *dest_ip, uint16_t dest_port)
{
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS)
    {
        fprintf(stderr, "Invalid socket descriptor.\n");
        return -1;
    }
    KSocket *ks = &ksocket_table[sockfd];
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(src_port);
    if (inet_pton(AF_INET, src_ip, &addr.sin_addr) <= 0)
    {
        perror("Invalid source IP address");
        return -1;
    }
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Failed to bind UDP socket");
        return -1;
    }
    ks->is_bound = 1;
    strncpy(ks->src_ip, src_ip, sizeof(ks->src_ip));
    ks->src_port = src_port;
    strncpy(ks->dest_ip, dest_ip, sizeof(ks->dest_ip));
    ks->dest_port = dest_port;
    return 0;
}

// k_sendto implements reliability. For data messages this function pre-appends a frame header and
// retransmits until a matching ACK is received. Control messages (EOF, FIN, etc.) are sent as-is.
int k_sendto(int sockfd, const void *message, size_t length)
{
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS)
    {
        fprintf(stderr, "Invalid socket descriptor.\n");
        return -1;
    }
    KSocket *ks = &ksocket_table[sockfd];
    if (!ks->is_bound)
    {
        fprintf(stderr, "Socket is not bound.\n");
        return -1;
    }
    // Set up the destination address.
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(ks->dest_port);
    if (inet_pton(AF_INET, ks->dest_ip, &dest_addr.sin_addr) <= 0)
    {
        perror("Invalid destination IP address");
        return -1;
    }
    // Check for control messages. These bypass the reliability logic.
    if ((length == 4 && strncmp(message, "EOF", 3) == 0) ||
        (length >= 7 && strncmp(message, "EOF_ACK", 7) == 0) ||
        (length == 3 && strncmp(message, "FIN", 3) == 0) ||
        (length >= 7 && strncmp(message, "FIN_ACK", 7) == 0))
    {
        ssize_t sent_bytes = sendto(sockfd, message, length, 0,
                                    (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent_bytes < 0)
        {
            perror("Failed to send control message");
            return -1;
        }
        return sent_bytes;
    }

    // We assume ACK frames built by our protocol have a payload "ACK" (3 bytes)
    // so that total length equals sizeof(FrameHeader) + 3.
    if (length == sizeof(FrameHeader) + 3)
    {
        FrameHeader *hdr = (FrameHeader *)message;
        if (hdr->type == FRAME_ACK)
        {
            // Send ACK frame directly without retransmission or extra logging.
            ssize_t sent_bytes = sendto(sockfd, message, length, 0,
                                        (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (sent_bytes < 0)
            {
                perror("Failed to send ACK frame");
                return -1;
            }
            return sent_bytes;
        }
    }
    // For data messages, ensure the payload is acceptable.
    if (length > (size_t)PAYLOAD_SIZE)
    {
        fprintf(stderr, "Payload too large.\n");
        return -1;
    }
    size_t frame_size = sizeof(FrameHeader) + length;
    char *frame = malloc(frame_size);
    if (!frame)
    {
        perror("Memory allocation failed");
        return -1;
    }
    FrameHeader *header = (FrameHeader *)frame;
    header->type = FRAME_DATA;
    header->seq = ks->next_seq;
    header->length = (uint16_t)length;
    memcpy(frame + sizeof(FrameHeader), message, length);
    // Reliability loop: send until a valid ACK is received.
    int ack_received = 0;
    while (!ack_received)
    {
        ssize_t sent_bytes = sendto(sockfd, frame, frame_size, 0,
                                    (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent_bytes < 0)
        {
            perror("Failed to send data frame");
            free(frame);
            return -1;
        }
        if (strcmp(message, "EOF_ACK") != 0 && strcmp(message, "FIN"))
            printf("Sent frame with sequence %d.\n", header->seq);
        // Wait for ACK using select() with a timeout.
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        struct timeval timeout;
        timeout.tv_sec = T;
        timeout.tv_usec = 0;
        int sel = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (sel == 0)
        {
            printf("Timeout waiting for ACK for seq %d. Retrying...\n", header->seq);
            continue;
        }
        else if (sel < 0)
        {
            perror("select() error");
            free(frame);
            return -1;
        }
        char ack_buffer[MESSAGE_SIZE];
        ssize_t rcv_bytes = recvfrom(sockfd, ack_buffer, sizeof(ack_buffer), 0, NULL, NULL);
        if (rcv_bytes >= (ssize_t)sizeof(FrameHeader))
        {
            FrameHeader *ack = (FrameHeader *)ack_buffer;
            if (ack->type == FRAME_ACK && ack->seq == header->seq)
            {
                if (strcmp(message, "EOF_ACK") != 0 && strcmp(message, "FIN"))
                    printf("Received ACK for seq %d.\n", header->seq);
                ack_received = 1;
            }
            else
            {
                printf("Invalid ACK received. Retrying...\n");
            }
        }
    }
    free(frame);
    ks->next_seq++; // Advance sequence for next data frame.
    return (int)length;
}

// k_recvfrom receives a message from the network. For data frames, it automatically sends back an ACK.
// It strips off the header before delivering data to the user.
// Control messages are forwarded directly.
int k_recvfrom(int sockfd, void *buffer, size_t length)
{
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    char recv_buffer[MESSAGE_SIZE];
    ssize_t rcv_bytes = recvfrom(sockfd, recv_buffer, MESSAGE_SIZE, 0,
                                 (struct sockaddr *)&sender_addr, &addr_len);
    if (rcv_bytes < 0)
    {
        perror("Failed to receive message");
        return -1;
    }
    // Handle control messages directly.
    if (rcv_bytes >= 3)
    {
        if ((strncmp(recv_buffer, "EOF", 3) == 0) ||
            (strncmp(recv_buffer, "EOF_ACK", 7) == 0) ||
            (strncmp(recv_buffer, "FIN", 3) == 0) ||
            (strncmp(recv_buffer, "FIN_ACK", 7) == 0))
        {
            if ((size_t)rcv_bytes > length)
                rcv_bytes = length;
            memcpy(buffer, recv_buffer, rcv_bytes);
            return (int)rcv_bytes;
        }
    }
    // For data frames, simulate packet loss.
    if (dropMessage(P))
    {
        printf("Packet dropped (simulated).\n");
        return -1;
    }
    if (rcv_bytes < (ssize_t)sizeof(FrameHeader))
    {
        fprintf(stderr, "Received packet too small.\n");
        return -1;
    }
    FrameHeader *header = (FrameHeader *)recv_buffer;
    if (header->type != FRAME_DATA)
    {
        if ((size_t)rcv_bytes > length)
            rcv_bytes = length;
        memcpy(buffer, recv_buffer, rcv_bytes);
        return (int)rcv_bytes;
    }
    // --- Send back an ACK that now includes a payload ("ACK") for visibility in Wireshark ---
    {
        const char *ack_payload = "ACK";
        uint16_t ack_payload_len = 3;
        uint8_t ack_frame[sizeof(FrameHeader) + ack_payload_len];
        FrameHeader *ack_hdr = (FrameHeader *)ack_frame;
        ack_hdr->type = FRAME_ACK;
        ack_hdr->seq = header->seq;
        ack_hdr->length = ack_payload_len;
        memcpy(ack_frame + sizeof(FrameHeader), ack_payload, ack_payload_len);

        KSocket *ks = &ksocket_table[sockfd];
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(ks->dest_port);
        if (inet_pton(AF_INET, ks->dest_ip, &dest_addr.sin_addr) <= 0)
        {
            perror("Invalid destination IP address");
        }
        else
        {
            sendto(sockfd, ack_frame, sizeof(ack_frame), 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        }
    }
    // --- End of ACK section ---

    // Copy only the payload into the provided buffer (strip the header).
    size_t payload_len = header->length;
    if (payload_len > length)
    {
        payload_len = length;
    }
    memcpy(buffer, recv_buffer + sizeof(FrameHeader), payload_len);
    // Return only the payload length so that the user writes exactly that many bytes.
    return (int)payload_len;
}

// k_close wraps close() on the underlying UDP socket.
int k_close(int sockfd)
{
    if (close(sockfd) < 0)
    {
        perror("Failed to close socket");
        return -1;
    }
    return 0;
}
