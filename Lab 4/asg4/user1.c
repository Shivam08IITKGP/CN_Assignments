/*      This is user2.c the sender
*/
#include "ksocket.h"

#define MESSAGE_SIZE 512
#define PAYLOAD_SIZE (MESSAGE_SIZE - sizeof(FrameHeader))       
/*
I am also attaching the FrameHeader to the payload, so 
the payload size is reduced by the size of the FrameHeader
*/

#define IP1 "127.0.0.1"  // Sender's IP
#define PORT1 5000       // Sender's Port
#define IP2 "127.0.0.1"  // Receiver's IP
#define PORT2 6000       // Receiver's Port

// FrameHeader structure (for internal use)
#pragma pack(push, 1)
typedef struct {
    uint8_t type;      // FRAME_DATA, FRAME_ACK, etc.
    uint8_t seq;       // Sequence number
    uint16_t length;   // Payload length (in bytes)
} FrameHeader;
#pragma pack(pop)

int main(void) {
    int sockfd;
    FILE *file;
    size_t bytes_read;

    // Create a KTP socket.
    sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("Failed to create KTP socket");
        exit(EXIT_FAILURE);
    }
    // Bind to local address (PORT1) and set destination (PORT2).
    if (k_bind(sockfd, IP1, PORT1, IP2, PORT2) < 0) {
        perror("Failed to bind KTP socket");
        exit(EXIT_FAILURE);
    }
    // Open the file to send.
    file = fopen("input.txt", "rb");
    if (!file) {
        perror("Failed to open input.txt");
        exit(EXIT_FAILURE);
    }
    char payload[PAYLOAD_SIZE];
    // Read and send file data reliably.
    while ((bytes_read = fread(payload, 1, PAYLOAD_SIZE, file)) > 0) {
        if (k_sendto(sockfd, payload, bytes_read) < 0) {
            perror("Failed to send data frame");
            fclose(file);
            exit(EXIT_FAILURE);
        }
        printf("Sent %zu bytes of file data.\n", bytes_read);
    }
    fclose(file);
    printf("File data transmission completed.\n");

    // Connection Closure Protocol (Sender side)
    // 1. Send "EOF" until "EOF_ACK" is received.
    int eof_ack_received = 0;
    while (!eof_ack_received) {
        if (k_sendto(sockfd, "EOF", 4) < 0) {
            perror("Failed to send EOF");
        } else {
            printf("Sent control message: EOF\n");
        }
        char ctrl_buffer[64];
        if (k_recvfrom(sockfd, ctrl_buffer, sizeof(ctrl_buffer)) > 0 &&
            strncmp(ctrl_buffer, "EOF_ACK", 7) == 0) {
            printf("Received control message: EOF_ACK\n");
            eof_ack_received = 1;
        }
    }
    // 2. Wait for "FIN" from receiver; then send "FIN_ACK".
    int fin_received = 0;
    while (!fin_received) {
        char ctrl_buffer[64];
        if (k_recvfrom(sockfd, ctrl_buffer, sizeof(ctrl_buffer)) > 0 &&
            strncmp(ctrl_buffer, "FIN", 3) == 0) {
            printf("Received control message: FIN\n");
            fin_received = 1;
            if (k_sendto(sockfd, "FIN_ACK", 8) < 0) {
                perror("Failed to send FIN_ACK");
            } else {
                printf("Sent control message: FIN_ACK\n");
            }
        }
    }
    if (k_close(sockfd) < 0) {
        perror("Failed to close KTP socket");
        exit(EXIT_FAILURE);
    }
    printf("Connection closed reliably.\n");
    return 0;
}
