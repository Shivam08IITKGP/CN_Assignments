#include "ksocket.h"

// Global error variable
int ksocket_errno;

// Define the shared_memory variable here
K_Socket k_sockets[MAX_KTP_SOCKETS];
SharedMemoryEntry *shared_memory = NULL;
sem_t *sem1 = NULL;
sem_t *sem2 = NULL;
sem_t *sem_SM = NULL;

// Define SHM_KEY to be the same as in initksocket.c
#define SHM_KEY 86969 // Shared memory key

// Function to simulate packet loss
int dropMessage(float p)
{
    float probability = ((float)rand() / (float)RAND_MAX);
    return probability < p ? 1 : 0;
}

// Initialize shared memory
int initialize_shared_memory()
{
    // Get the shared memory segment created by initksocket
    sem_SM = sem_open("sem_SM", 0);
    if (sem_SM == SEM_FAILED)
    {
        perror("initialize_shared_memory: Failed to open sem_SM");
        return -1;
    }

    sem_wait(sem_SM);

    int shmid = shmget(SHM_KEY, MAX_KTP_SOCKETS * sizeof(SharedMemoryEntry), 0666);
    if (shmid < 0)
    {
        perror("Failed to get shared memory segment");
        return -1;
    }

    // Attach shared memory
    shared_memory = (SharedMemoryEntry *)shmat(shmid, NULL, 0);
    if (shared_memory == (void *)-1)
    {
        perror("Failed to attach shared memory");
        return -1;
    }
    sem_post(sem_SM);
    sem_close(sem_SM);
    // printf("Attached to shared memory successfully\n");
    return 0;
}

// Send a message using the KTP socket
int k_sendto(int sockfd, const void *message, size_t length)
{
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS)
    {
        printf("Invalid socket descriptor.\n");
        return -1;
    }

    sem_SM = sem_open("sem_SM", 0);
    if (sem_SM == SEM_FAILED)
    {
        printf("k_sendto: Failed to open sem_SM.\n");
        return -1;
    }
    
    // Check if socket is allocated to this process
    sem_wait(sem_SM);
    shared_memory[sockfd].pid = getpid();
    shared_memory[sockfd].is_free = 0;
    shared_memory[sockfd].udp_socket = k_sockets[sockfd].udp_socket;
    memcpy(shared_memory[sockfd].dest_ip, k_sockets[sockfd].dest_ip, 15);
    shared_memory[sockfd].dest_port = k_sockets[sockfd].dest_port;
    memcpy(shared_memory[sockfd].src_ip, k_sockets[sockfd].src_ip, 15);
    shared_memory[sockfd].src_port = k_sockets[sockfd].src_port;

    if (shared_memory[sockfd].is_free == 1 || shared_memory[sockfd].pid != getpid())
    {
        printf("Socket not owned by this process or is free.\n");
        sem_post(sem_SM);
        sem_close(sem_SM);
        // ksocket_errno = EBADF;
        return -1;
    }

    if (length > MESSAGE_SIZE - 2) // Reserve space for seq number and null terminator
    {
        printf("Message too long.\n");
        sem_post(sem_SM);
        sem_close(sem_SM);
        // ksocket_errno = ENOSPACE;
        return -1;
    }
    // Find a free buffer slot in the send buffer
    int free_index = -1;
    for (int i = 0; i < 10; i++)
    {
        if (shared_memory[sockfd].sbuff_free[i])
        {
            free_index = i;
            break;
        }
    }

    if (free_index == -1)
    {
        // No free space in send buffer
        printf("No free space in send buffer.\n");
        sem_post(sem_SM);
        sem_close(sem_SM);
        // ksocket_errno = ENOSPACE;
        return -1;
    }

    // Get the next sequence number
    uint8_t seq_num = shared_memory[sockfd].max_seq_number_yet++;

    // Create message with sequence number
    char seq_message[MESSAGE_SIZE];
    seq_message[0] = seq_num;
    memcpy(seq_message + 1, message, length);
    seq_message[length + 1] = '\0';
    shared_memory[sockfd].swnd.index_seq_num[seq_num] = free_index;
    shared_memory[sockfd].sbuff_free[free_index] = false;
    memcpy(shared_memory[sockfd].send_buffer[free_index], seq_message, MESSAGE_SIZE);
    sem_post(sem_SM);
    sem_close(sem_SM);

    printf("Message queued for sending with sequence number %d.\n", seq_num);
    return length; // Return number of bytes queued
}

// Receive a message using the KTP socket
int k_recvfrom(int sockfd, void *buffer, size_t length)
{
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS)
    {
        printf("Invalid socket descriptor.\n");
        // ksocket_errno = EBADF;
        return -1;
    }

    sem_SM = sem_open("sem_SM", 0);
    if (sem_SM == SEM_FAILED)
    {
        printf("k_recvfrom: Failed to open sem_SM");
        return -1;
    }

    // Check if socket is allocated to this process
    sem_wait(sem_SM);

    if (shared_memory[sockfd].is_free == 1 || shared_memory[sockfd].pid != getpid())
    {
        printf("Socket not owned by this process or is free.\n");
        sem_post(sem_SM);
        sem_close(sem_SM);
        // ksocket_errno = EBADF;
        return -1;
    }

    // Check if any messages are available
    int index = -1;
    int min_seq_num = 256;
    for (int i = 0; i < 10; i++)
    {
        if (!shared_memory[sockfd].rbuff_free[i])
        {
            if (index != -1 && shared_memory[sockfd].recv_buffer[i][0] < min_seq_num)
            {
                min_seq_num = shared_memory[sockfd].recv_buffer[i][0];
                index = i;
            }
            else if (index == -1)
            {
                min_seq_num = shared_memory[sockfd].recv_buffer[i][0];
                index = i;
            }
        }
    }

    if (min_seq_num > shared_memory[sockfd].rwnd.start_sequence || min_seq_num == 256)
    {
        // printf("min_seq_num = %d\n", min_seq_num);
        // printf("shared_memory[sockfd].rwnd.start_sequence = %d\n", shared_memory[sockfd].rwnd.start_sequence);
        sem_post(sem_SM);
        sem_close(sem_SM);
        // ksocket_errno = ENOMESSAGE;
        return -1;
    }

    if (index == -1)
    {
        sem_post(sem_SM);
        sem_close(sem_SM);
        // ksocket_errno = ENOMESSAGE;
        return -1;
    }

    if (shared_memory[sockfd].rwnd.start_sequence == min_seq_num)
    {
        shared_memory[sockfd].rwnd.start_sequence++;
        shared_memory[sockfd].rwnd.start_sequence %= 256;
    }
    // Copy message to buffer (skip the sequence number byte)
    size_t msg_len = strlen(shared_memory[sockfd].recv_buffer[index]);
    msg_len--;

    memcpy(buffer, shared_memory[sockfd].recv_buffer[index] + 1, msg_len);
    ((char *)buffer)[msg_len] = '\0'; // Ensure null termination

    // Mark buffer as free
    shared_memory[sockfd].rbuff_free[index] = true;
    memset(shared_memory[sockfd].recv_buffer[index], 0, MESSAGE_SIZE);

    // Update receiver window
    shared_memory[sockfd].rwnd.size++;
    shared_memory[sockfd].swnd.size++;
    shared_memory[sockfd].swnd.size = shared_memory[sockfd].swnd.size > 10 ? 10 : shared_memory[sockfd].swnd.size;
    shared_memory[sockfd].rwnd.size = shared_memory[sockfd].rwnd.size > 10 ? 10 : shared_memory[sockfd].rwnd.size;

    sem_post(sem_SM);
    sem_close(sem_SM);

    return msg_len; // Return actual message length
}

// Creating a KTP Socket
int k_socket(int domain, int type, int protocol)
{
    sem_SM = sem_open("sem_SM", 0);
    if (sem_SM == SEM_FAILED)
    {
        perror("k_socket: Failed to open sem_SM");
        return -1;
    }

    if (type != SOCK_KTP)
    {
        // errno = EINVAL; // Invalid argument
        return -1;
    }

    // Initialize shared memory if not already done

    // Find a free shared memory entry
    for (int i = 0; i < MAX_KTP_SOCKETS; i++)
    {
        sem_wait(sem_SM);
        if (shared_memory[i].is_free == 1)
        {
            // Initialize socket entry
            shared_memory[i].is_free = 0;
            shared_memory[i].pid = getpid(); // Store process ID

            // Initialize other fields
            shared_memory[i].src_ip[0] = '\0';
            shared_memory[i].src_port = 0;
            shared_memory[i].dest_ip[0] = '\0';
            shared_memory[i].dest_port = 0;
            shared_memory[i].swnd.size = 10;
            shared_memory[i].rwnd.size = 10;

            sem_post(sem_SM);
            printf("Socket %d created successfully.\n", i);
            sem_close(sem_SM);
            return i; // Return index as KTP socket descriptor
        }
        sem_post(sem_SM);
    }

    // ksocket_errno = ENOSPACE; // No space available
    return -1;
}

// Bind the KTP socket to an address and port
int k_bind(int sockfd, const char *src_ip, uint16_t src_port, const char *dest_ip, uint16_t dest_port)
{
    sem1 = sem_open("sem1", 0);
    if (sem1 == SEM_FAILED)
    {
        perror("k_bind: Failed to open sem1");
        return -1;
    }

    sem2 = sem_open("sem2", 0);
    if (sem2 == SEM_FAILED)
    {
        perror("k_bind: Failed to open sem2");
        sem_close(sem1);
        return -1;
    }

    sem_SM = sem_open("sem_SM", 0);
    if (sem_SM == SEM_FAILED)
    {
        perror("Failed to create semaphore sem2");
        sem_close(sem1);
        sem_close(sem2);
        sem_unlink("sem1");
        sem_unlink("sem2");
        return EXIT_FAILURE;
    }

    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS)
    {
        fprintf(stderr, "Invalid socket descriptor.\n");
        sem_close(sem1);
        sem_close(sem2);
        return -1;
    }

    // Check if socket is allocated to this process
    sem_wait(sem_SM);
    if (shared_memory[sockfd].is_free == 1)
    {
        fprintf(stderr, "Socket not owned by this process or is free.\n");
        sem_close(sem1);
        sem_close(sem2);
        sem_close(sem_SM);
        return -1;
    }
    strncpy(shared_memory[sockfd].src_ip, src_ip, 15);
    shared_memory[sockfd].src_ip[15] = '\0';
    shared_memory[sockfd].src_port = src_port;
    strncpy(shared_memory[sockfd].dest_ip, dest_ip, 15);
    shared_memory[sockfd].dest_ip[15] = '\0';
    shared_memory[sockfd].dest_port = dest_port;
    shared_memory[sockfd].is_free = 2; // Ready to bind
    sem_post(sem_SM);

    // Release sem2 to signal bind_function to run
    sem_post(sem2);

    // Wait on sem1 before proceeding
    sem_wait(sem1);

    // put it in ksocket table
    sem_wait(sem_SM);
    k_sockets[sockfd].udp_socket = shared_memory[sockfd].udp_socket;
    strncpy(k_sockets[sockfd].dest_ip, shared_memory[sockfd].dest_ip, 15);
    k_sockets[sockfd].dest_ip[15] = '\0';
    k_sockets[sockfd].dest_port = shared_memory[sockfd].dest_port;
    strncpy(k_sockets[sockfd].src_ip, shared_memory[sockfd].src_ip, 15);
    k_sockets[sockfd].src_ip[15] = '\0';
    k_sockets[sockfd].src_port = shared_memory[sockfd].src_port;
    
    printf("k_bind: bind_function completed, continuing\n");
    printf("Socket got Index %d\n", sockfd);
    printf("Source IP: %s\n", src_ip);
    printf("Source Port: %d\n", src_port);
    printf("Destination IP: %s\n", dest_ip);
    printf("Destination Port: %d\n", dest_port);
    printf("Shared Memory is_free: %d\n", shared_memory[sockfd].is_free);
    sem_post(sem_SM);

    sem_close(sem1);
    sem_close(sem2);
    sem_close(sem_SM);

    return 0; // Success
}

// Close the KTP socket
int k_close(int sockfd)
{
    if (sockfd < 0 || sockfd >= MAX_KTP_SOCKETS)
    {
        errno = EBADF; // Bad file descriptor
        return -1;
    }

    sem_wait(sem_SM);
    if (shared_memory[sockfd].is_free == 1)
    {
        sem_post(sem_SM);
        // errno = EBADF; // Bad file descriptor
        return -1;
    }

    if (shared_memory[sockfd].pid != getpid())
    {
        // Prevent closing sockets created by other processes
        sem_post(sem_SM);
        fprintf(stderr, "Cannot close socket created by another process.\n");
        // errno = EPERM; // Operation not permitted
        return -1;
    }

    // Close UDP socket
    close(getpid());

    // Mark entry as free
    shared_memory[sockfd].is_free = 1;
    shared_memory[sockfd].pid = 0;;

    sem_post(sem_SM);
    sem_close(sem_SM);
    return 0; // Success
}