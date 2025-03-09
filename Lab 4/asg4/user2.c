/*      This is user2.c the receiver.

*/

#include "ksocket.h"

#define MESSAGE_SIZE 512

#define IP1 "127.0.0.1"  // Receiver's IP
#define PORT1 6000       // Receiver's Port
#define IP2 "127.0.0.1"  // Sender's IP
#define PORT2 5000       // Sender's Port

int main(void) {
    int sockfd;
    FILE *file;
    char buffer[MESSAGE_SIZE];
    int bytes_received;

    // Create a KTP socket.
    sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("Failed to create KTP socket");
        exit(EXIT_FAILURE);
    }
    // Bind the receiver’s socket: local PORT1, sender’s info is IP2:PORT2.
    if (k_bind(sockfd, IP1, PORT1, IP2, PORT2) < 0) {
        perror("Failed to bind KTP socket");
        exit(EXIT_FAILURE);
    }
    // Open output file.
    file = fopen("output.txt", "wb");
    if (!file) {
        perror("Failed to open output.txt");
        exit(EXIT_FAILURE);
    }
    printf("Waiting to receive data frames...\n");
    while (1) {
        bytes_received = k_recvfrom(sockfd, buffer, MESSAGE_SIZE);
        if (bytes_received < 0) {
            // Skip on error (e.g., simulated packet drop)
            continue;
        }
        // Check for a control message indicating end-of-file.
        if (strncmp(buffer, "EOF", 3) == 0) {
            printf("Received control message: EOF\n");
            // Respond with EOF_ACK.
            if (k_sendto(sockfd, "EOF_ACK", 8) < 0) {
                perror("Failed to send EOF_ACK");
            } else {
                printf("Sent control message: EOF_ACK\n");
            }
            break;  // Exit reception loop.
        }
        // Write the received payload (of length bytes_received) to file.
        fwrite(buffer, 1, bytes_received, file);
        fflush(file);
        printf("Received data frame: %d payload bytes.\n", bytes_received);
    }
    fclose(file);
    printf("Data reception completed.\n");

    // --- Connection Closure Protocol (Receiver Side) ---
    // After finishing data reception, repeatedly send "FIN" until "FIN_ACK" is received.
    int fin_ack_received = 0;
    while (!fin_ack_received) {
        if (k_sendto(sockfd, "FIN", 4) < 0) {
            perror("Failed to send FIN");
        } else {
            printf("Sent control message: FIN\n");
        }
        bytes_received = k_recvfrom(sockfd, buffer, MESSAGE_SIZE);
        if (bytes_received > 0 && strncmp(buffer, "FIN_ACK", 7) == 0) {
            printf("Received control message: FIN_ACK\n");
            fin_ack_received = 1;
        }
    }
    // Close the socket.
    if (k_close(sockfd) < 0) {
        perror("Failed to close KTP socket");
        exit(EXIT_FAILURE);
    }
    printf("Connection closed reliably.\n");
    return 0;
}
