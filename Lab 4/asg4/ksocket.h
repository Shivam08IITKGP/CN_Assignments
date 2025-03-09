#ifndef KSOCKET_H
#define KSOCKET_H

#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// Constants
#define SOCK_KTP 1        // Custom socket type for KTP
#define T 5               // Timeout duration in seconds
#define P 0.05             // Default probability for dropMessage()
#define MAX_KTP_SOCKETS 10
#define MESSAGE_SIZE 512  // Maximum message size (includes protocol header)

// Error codes
#define ENOSPACE   -1    // Buffer has not enough space
#define ENOTBOUND  -2    // Socket not bound
#define ENOMESSAGE -3    // No message available

// Data structure for KTP socket management
typedef struct {
    int is_bound;         // Whether the socket is bound
    char src_ip[16];      // Source IP address (IPv4)
    uint16_t src_port;    // Source port number
    char dest_ip[16];     // Destination IP address (IPv4)
    uint16_t dest_port;   // Destination port number
    uint8_t next_seq;     // Next sequence number for outgoing data frames
} KSocket;

// Function prototypes
int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, const char *src_ip, uint16_t src_port, const char *dest_ip, uint16_t dest_port);
int k_sendto(int sockfd, const void *message, size_t length);
int k_recvfrom(int sockfd, void *buffer, size_t length);
int k_close(int sockfd);
int dropMessage(float p);

#endif /* KSOCKET_H */
