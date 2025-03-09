#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <semaphore.h>

// Constants
#define SOCK_KTP 3 // Custom socket type for KTP
#define T 5        // Timeout duration in seconds
#define P 0.3      // Default probability for dropMessage()

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
    signed char index_seq_num[256]; // Sequence numbers of unacknowledged messages
    int size;                   // Current size of the sender window
    bool valid_seq_num[256];
    int start_sequence;
} SenderWindow;

// Receiver Window (rwnd) structure
typedef struct
{
    signed char index_seq_num[256]; // Sequence numbers of received but unacknowledged messages
    int size;                   // Current size of the receiver window
    bool valid_seq_num[256];
    int start_sequence;
} ReceiverWindow;

// Shared Memory (SM) entry for a single KTP socket
typedef struct
{
    int is_free;               // Whether this socket entry is free or allocated or ready to bind
    int pid;                   // Process ID that created this socket
    int udp_socket;            // Corresponding UDP socket file descriptor
    char dest_ip[16];          // Destination IP address (IPv4)
    uint16_t dest_port;        // Destination port number
    char src_ip[16];           // Source IP address (IPv4)
    uint16_t src_port;         // Source port number
    char send_buffer[10][512]; // Sender-side message buffer (10 messages * 512 bytes)
    bool sbuff_free[10];
    char recv_buffer[10][512]; // Receiver-side message buffer (10 messages * 512 bytes)
    bool rbuff_free[10];
    SenderWindow swnd;   // Sender window structure
    ReceiverWindow rwnd; // Receiver window structure
    int max_seq_number_yet;
    long int send_time[10]; // Time at which the message was sent
    int last_ack_received;
    bool no_space_flags;
} SharedMemoryEntry;

// K socket table
typedef struct
{
    int udp_socket;
    char dest_ip[16];
    uint16_t dest_port;
    char src_ip[16];
    uint16_t src_port;

} K_Socket;

// External declarations
extern SharedMemoryEntry *shared_memory;
extern sem_t *sem1, *sem2, *sem_SM;
extern int ksocket_errno;
// extern K_Socket *k_sockets;

// Function prototypes
int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, const char *src_ip, uint16_t src_port, const char *dest_ip, uint16_t dest_port);
int k_sendto(int sockfd, const void *message, size_t length);
int k_recvfrom(int sockfd, void *buffer, size_t length);
int k_close(int sockfd);
int dropMessage(float p);
int initialize_shared_memory();

// Thread functions
void *thread_R_function(void *arg);
void *thread_S_function(void *arg);
void *garbage_collector(void *arg);
