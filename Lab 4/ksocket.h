#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

// Constants
#define SOCK_KTP 1 // Custom socket type for KTP
#define T 5        // Timeout duration in seconds
#define P 0.05     // Default probability for dropMessage()

// Error codes
#define ENOSPACE -1   // No space available in the buffer or shared memory
#define ENOTBOUND -2  // Socket is not bound to an address/port
#define ENOMESSAGE -3 // No message available in the receiver buffer

#define MAX_KTP_SOCKETS 10
#define MESSAGE_SIZE 512

// Data structures for KTP socket management

// Sender Window (swnd) structure
typedef struct
{
    uint8_t seq_nums[10]; // Sequence numbers of unacknowledged messages
    uint8_t size;         // Current size of the sender window
} SenderWindow;

// Receiver Window (rwnd) structure
typedef struct
{
    uint8_t seq_nums[10]; // Sequence numbers of received but unacknowledged messages
    uint8_t size;         // Current size of the receiver window
} ReceiverWindow;

// Shared Memory (SM) entry for a single KTP socket
typedef struct
{
    int is_free;            // Whether this socket entry is free or allocated
    int process_id;         // Process ID that created this socket
    int udp_socket;         // Corresponding UDP socket file descriptor
    char dest_ip[16];       // Destination IP address (IPv4)
    uint16_t dest_port;     // Destination port number
    char src_ip[16];        // Source IP address (IPv4)
    uint16_t src_port;      // Source port number
    char send_buffer[5120]; // Sender-side message buffer (10 messages * 512 bytes)
    char recv_buffer[5120]; // Receiver-side message buffer (10 messages * 512 bytes)
    SenderWindow swnd;      // Sender window structure
    ReceiverWindow rwnd;    // Receiver window structure
} SharedMemoryEntry;

// Function prototypes

/**
 * Creates a KTP socket.
 */
int k_socket(int domain, int type, int protocol);

/**
 * Binds a KTP socket to a specific address and port.
 */
int k_bind(int sockfd, const char *src_ip, uint16_t src_port, const char *dest_ip, uint16_t dest_port);

/**
 * Sends a message using the KTP socket.
 */
int k_sendto(int sockfd, const void *message, size_t length);

/**
 * Receives a message using the KTP socket.
 */
int k_recvfrom(int sockfd, void *buffer, size_t length);

/**
 * Closes a KTP socket and frees associated resources.
 */
int k_close(int sockfd);

/**
 * Simulates an unreliable link by randomly dropping messages based on a given probability.
 */
int dropMessage(float p);
extern SharedMemoryEntry *shared_memory;
extern pthread_mutex_t shm_mutex;