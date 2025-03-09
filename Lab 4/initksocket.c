#include "ksocket.h"

SharedMemoryEntry *shared_memory = NULL;
pthread_mutex_t shm_mutex = PTHREAD_MUTEX_INITIALIZER;

// Constants
#define SHM_KEY 12345 // Shared memory key

// Thread identifiers
pthread_t thread_R, thread_S;
pid_t garbage_collector_pid;

// Function prototypes
void *thread_R_function(void *arg);
void *thread_S_function(void *arg);
void garbage_collector();

// Initialize KTP socket system
int init_ksocket()
{
    // Create shared memory for KTP sockets
    int shmid = shmget(SHM_KEY, MAX_KTP_SOCKETS * sizeof(SharedMemoryEntry), IPC_CREAT | 0666);
    if (shmid < 0)
    {
        perror("Failed to create shared memory");
        return -1;
    }

    // Attach shared memory
    shared_memory = (SharedMemoryEntry *)shmat(shmid, NULL, 0);
    if (shared_memory == (void *)-1)
    {
        perror("Failed to attach shared memory");
        return -1;
    }

    // Initialize shared memory entries
    pthread_mutex_lock(&shm_mutex);
    for (int i = 0; i < MAX_KTP_SOCKETS; i++)
    {
        shared_memory[i].is_free = 1; // Mark all entries as free
    }
    pthread_mutex_unlock(&shm_mutex);

    // Create thread R (Receiver handler)
    if (pthread_create(&thread_R, NULL, thread_R_function, NULL) != 0)
    {
        perror("Failed to create thread R");
        return -1;
    }

    // Create thread S (Sender handler)
    if (pthread_create(&thread_S, NULL, thread_S_function, NULL) != 0)
    {
        perror("Failed to create thread S");
        return -1;
    }

    // Fork garbage collector process
    garbage_collector_pid = fork();
    if (garbage_collector_pid == 0)
    {
        garbage_collector(); // Child process runs garbage collector
        exit(0);
    }
    else if (garbage_collector_pid < 0)
    {
        perror("Failed to fork garbage collector process");
        return -1;
    }

    printf("KTP socket system initialized successfully.\n");
    return 0;
}

// Thread R: Handles incoming messages and ACKs
void *thread_R_function(void *arg)
{
    fd_set read_fds;
    struct timeval timeout;

    while (1)
    {
        FD_ZERO(&read_fds);

        pthread_mutex_lock(&shm_mutex);

        int max_fd = -1;
        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            if (!shared_memory[i].is_free)
            {
                FD_SET(shared_memory[i].udp_socket, &read_fds);
                if (shared_memory[i].udp_socket > max_fd)
                {
                    max_fd = shared_memory[i].udp_socket;
                }
            }
        }

        pthread_mutex_unlock(&shm_mutex);

        timeout.tv_sec = T / 2; // Timeout duration
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity > 0)
        {
            pthread_mutex_lock(&shm_mutex);
            for (int i = 0; i < MAX_KTP_SOCKETS; i++)
            {
                if (!shared_memory[i].is_free && FD_ISSET(shared_memory[i].udp_socket, &read_fds))
                {
                    char buffer[512];
                    struct sockaddr_in sender_addr;
                    socklen_t addr_len = sizeof(sender_addr);

                    ssize_t bytes_received = recvfrom(shared_memory[i].udp_socket, buffer, sizeof(buffer), 0,
                                                      (struct sockaddr *)&sender_addr, &addr_len);

                    if (bytes_received > 0 && !dropMessage(P))
                    {
                        printf("Thread R: Received message on socket %d\n", i);

                        // Extract sequence number
                        uint8_t seq_num = buffer[0]; // Assume first byte is sequence number

                        // Send ACK back
                        char ack_msg[4] = "ACK";
                        ack_msg[3] = seq_num;

                        sendto(shared_memory[i].udp_socket, ack_msg, 4, 0,
                               (struct sockaddr *)&sender_addr, addr_len);

                        printf("Thread R: Sent ACK for seq %d on socket %d\n", seq_num, i);
                    }
                }
            }
            pthread_mutex_unlock(&shm_mutex);
        }
    }

    return NULL;
}

// Thread S: Handles retransmissions and timeouts
void *thread_S_function(void *arg)
{
    while (1)
    {
        sleep(T / 2); // Check every T/2 seconds

        pthread_mutex_lock(&shm_mutex);

        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            if (!shared_memory[i].is_free)
            {
                printf("Thread S: Checking timeouts for socket %d\n", i);

                for (int j = 0; j < shared_memory[i].swnd.size; j++)
                {
                    uint8_t seq_num = shared_memory[i].swnd.seq_nums[j];

                    printf("Thread S: Retransmitting seq %d on socket %d\n", seq_num, i);

                    struct sockaddr_in dest_addr;
                    memset(&dest_addr, 0, sizeof(dest_addr));
                    dest_addr.sin_family = AF_INET;
                    dest_addr.sin_port = htons(shared_memory[i].dest_port);
                    inet_pton(AF_INET, shared_memory[i].dest_ip, &dest_addr.sin_addr);

                    ssize_t sent_bytes = sendto(shared_memory[i].udp_socket,
                                                shared_memory[i].send_buffer + (seq_num * MESSAGE_SIZE),
                                                MESSAGE_SIZE, 0,
                                                (struct sockaddr *)&dest_addr,
                                                sizeof(dest_addr));

                    if (sent_bytes < 0)
                    {
                        perror("Thread S: Failed to retransmit message");
                    }
                }
            }
        }

        pthread_mutex_unlock(&shm_mutex);
    }

    return NULL;
}

// Garbage collector process: Cleans up unused KTP sockets
void garbage_collector()
{
    while (1)
    {
        sleep(10); // Run every 10 seconds

        pthread_mutex_lock(&shm_mutex);

        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            if (!shared_memory[i].is_free && kill(shared_memory[i].process_id, 0) != 0)
            {
                printf("Garbage Collector: Cleaning up socket %d\n", i);
                close(shared_memory[i].udp_socket); // Close UDP socket
                shared_memory[i].is_free = 1;       // Mark entry as free
            }
        }

        pthread_mutex_unlock(&shm_mutex);
    }
}

// Main function to initialize KTP socket system
int main()
{
    if (init_ksocket() != 0)
    {
        fprintf(stderr, "Failed to initialize KTP socket system.\n");
        return EXIT_FAILURE;
    }

    // Wait for threads and garbage collector to run indefinitely.
    pthread_join(thread_R, NULL);
    pthread_join(thread_S, NULL);

    return EXIT_SUCCESS;
}
