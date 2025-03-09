#include "ksocket.h"
z
// Global error variable
int ksocket_errno;

typedef struct
{
    int is_bound;       // Whether the socket is bound
    char src_ip[16];    // Source IP address
    uint16_t src_port;  // Source port
    char dest_ip[16];   // Destination IP address
    uint16_t dest_port; // Destination port
} KSocket;

KSocket ksocket_table[MAX_KTP_SOCKETS];

// Function to simulate packet loss
int dropMessage(float p)
{
    float random = (float)rand() / RAND_MAX;
    return random < p ? 1 : 0;
}

// Create a KTP socket
int k_socket(int domain, int type, int protocol)
{
    if (type != SOCK_KTP)
    {
        errno = EINVAL; // Invalid argument
        return -1;
    }

    // Create a UDP socket
    int udp_sock = socket(domain, SOCK_DGRAM, protocol);
    if (udp_sock < 0)
    {
        perror("Failed to create UDP socket");
        return -1;
    }

    return udp_sock; // Return the UDP socket descriptor
}

// Bind the KTP socket to an address and port
int k_bind(int sockfd, const char *src_ip, uint16_t src_port, const char *dest_ip, uint16_t dest_port)
{
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS)
    {
        fprintf(stderr, "Invalid socket descriptor.\n");
        return -1;
    }

    KSocket *ks = &ksocket_table[sockfd];

    // Bind source IP and port
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

    // Update shared memory structure
    ks->is_bound = 1;
    strncpy(ks->src_ip, src_ip, 16);
    ks->src_port = src_port;
    strncpy(ks->dest_ip, dest_ip, 16);
    ks->dest_port = dest_port;

    return 0; // Success
}

// Send a message using the KTP socket
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
        fprintf(stderr, "Socket is not bound to a destination address.\n");
        return -1;
    }

    if (length > MESSAGE_SIZE)
    {
        ksocket_errno = ENOSPACE;
        return -1;
    }

    pthread_mutex_lock(&shm_mutex);

    SharedMemoryEntry *entry = &shared_memory[sockfd];

    // Store message in send buffer
    int seq_num = entry->swnd.size; // Next sequence number
    memcpy(entry->send_buffer + (seq_num * MESSAGE_SIZE), message, length);

    // Store sequence number in sender window
    entry->swnd.seq_nums[seq_num] = seq_num;
    entry->swnd.size++;

    pthread_mutex_unlock(&shm_mutex);

    return sendto(sockfd, message, length, 0,
                  (struct sockaddr *)&(entry->dest_ip),
                  sizeof(struct sockaddr_in));
}

// Receive a message using the KTP socket
int k_recvfrom(int sockfd, void *buffer, size_t length)
{
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    // Receive a message using recvfrom()
    ssize_t received_bytes = recvfrom(sockfd, buffer, length, 0,
                                      (struct sockaddr *)&sender_addr,
                                      &addr_len);

    if (received_bytes < 0)
    {
        perror("Failed to receive message");
        ksocket_errno = ENOMESSAGE;
        return -1;
    }

    // Simulate packet loss using dropMessage()
    if (dropMessage(P))
    {
        printf("Packet dropped\n");
        return -1; // Simulate lost packet
    }

    return received_bytes; // Return number of bytes received
}

// Close the KTP socket
int k_close(int sockfd)
{
    if (close(sockfd) < 0)
    {
        perror("Failed to close socket");
        return -1;
    }

    return 0; // Success
}
