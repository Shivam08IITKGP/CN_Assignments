#include "ksocket.h"

// Constants
#define SHM_KEY 86969 // Shared memory key

// Garbage collector function: Cleans up unused KTP sockets

void *garbage_collector(void *arg)
{
    sem_SM = sem_open("sem_SM", 0);

    if (sem_SM == SEM_FAILED)
    {
        perror("k_socket: Failed to open sem_SM");
        return NULL;
    }

    while (1)
    {
        sleep(10); // Run every 10 seconds
        sem_wait(sem_SM);
        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            if (shared_memory[i].is_free == 1)
            {
                continue;
            }
            if (kill(shared_memory[i].pid, 0) == -1)
            {
                // Process does not exist
                printf("Garbage collector: Cleaning up socket %d\n", i);
                close(shared_memory[i].udp_socket);
                shared_memory[i].is_free = 1;
                shared_memory[i].pid = 0;
            }
        }
        sem_post(sem_SM);
    }
    sem_close(sem_SM);

    return NULL;
}

// Thread R: Handles incoming messages and timeouts
void *thread_R_function(void *arg)
{
    printf("Thread R started\n"); // Add this to verify thread is running
    sem_SM = sem_open("sem_SM", 0);

    if (sem_SM == SEM_FAILED)
    {
        perror("k_socket: Failed to open sem_SM");
        return NULL;
    }

    fd_set read_fds;
    struct timeval timeout;

    while (1)
    {
        FD_ZERO(&read_fds);
        int max_fd = 0;

        // Process each socket to check if they have their finally space left in the receive window

        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            sem_wait(sem_SM);
            // Check if socket is allocated to ANY process (not just this one)
            if (shared_memory[i].is_free == 0) // Remove the PID check
            {
                // Check if socket is valid by checking its socket fd

                // Sets read_fds as shared_memory[i].udp_socket
                FD_SET(shared_memory[i].udp_socket, &read_fds);
                if (shared_memory[i].udp_socket > max_fd)
                {
                    max_fd = shared_memory[i].udp_socket;
                }

                // Check if nospace flag was set but now there's space
                if (shared_memory[i].no_space_flags && shared_memory[i].rwnd.size > 0)
                {
                    // Send duplicate ACK with updated window size
                    struct sockaddr_in dest_addr;
                    memset(&dest_addr, 0, sizeof(dest_addr));
                    dest_addr.sin_family = AF_INET;
                    dest_addr.sin_port = htons(shared_memory[i].dest_port);
                    dest_addr.sin_addr.s_addr = inet_addr(shared_memory[i].dest_ip);

                    unsigned char ack_buffer[6];
                    printf("shared_memory[%d].last_ack_received = %d\n", i, shared_memory[i].last_ack_received);
                    ack_buffer[0] = shared_memory[i].last_ack_received;
                    ack_buffer[1] = shared_memory[i].rwnd.size;
                    ack_buffer[2] = 'A';
                    ack_buffer[3] = 'C';
                    ack_buffer[4] = 'K';
                    ack_buffer[5] = '#';

                    sendto(shared_memory[i].udp_socket, ack_buffer, sizeof(ack_buffer), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                    printf("Thread R: Sent duplicate ACK for seq_num %d with updated window size %d\n", shared_memory[i].last_ack_received, shared_memory[i].rwnd.size);

                    // Reset nospace flag
                    shared_memory[i].no_space_flags = 0;
                }
            }
            sem_post(sem_SM);
        }

        // Set timeout for select
        timeout.tv_sec = T / 2;
        timeout.tv_usec = 0;

        // Wait for any socket to become readable or timeout
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity > 0)
        {
            sem_wait(sem_SM);
            for (int i = 0; i < MAX_KTP_SOCKETS; i++)
            {
                if (FD_ISSET(shared_memory[i].udp_socket, &read_fds))
                {
                    unsigned char buffer[MESSAGE_SIZE];
                    struct sockaddr_in sender_addr;
                    socklen_t addr_len = sizeof(sender_addr);

                    // Receive message
                    ssize_t bytes_received = recvfrom(shared_memory[i].udp_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender_addr, &addr_len);
                    if (bytes_received > 0)
                    {
                        // Check if it's an ACK message (has "ACK#" string)
                        if (bytes_received == 6 && buffer[2] == 'A' && buffer[3] == 'C' && buffer[4] == 'K')
                        {
                            if (buffer[5] == '#')
                            {
                                int acked_seq_num = buffer[0];
                                int remote_window_size = buffer[1];
                                printf("Thread R: Received ACK message on socket %d, seq_num %d, window size %d\n", i, acked_seq_num, remote_window_size);

                                if (acked_seq_num <= 0 || acked_seq_num > 255)
                                {
                                    continue;
                                }
                                if (remote_window_size < 0 || remote_window_size > 10)
                                {
                                    continue;
                                }

                                int j = shared_memory[i].swnd.start_sequence;
                                shared_memory[i].swnd.size = remote_window_size;
                                shared_memory[i].last_ack_received = acked_seq_num > shared_memory[i].last_ack_received ? acked_seq_num : shared_memory[i].last_ack_received;
                                // I need to update the send times of the rest of the packets
                                while (j != (shared_memory[i].last_ack_received) % 255 + 1)
                                {
                                    shared_memory[i].swnd.valid_seq_num[j] = 0;
                                    memset(shared_memory[i].send_buffer[shared_memory[i].swnd.index_seq_num[j]], 0, MESSAGE_SIZE);
                                    shared_memory[i].send_time[shared_memory[i].swnd.index_seq_num[j]] = -1;
                                    shared_memory[i].sbuff_free[shared_memory[i].swnd.index_seq_num[j]] = 1;
                                    shared_memory[i].swnd.index_seq_num[j] = -1;
                                    j = 1 + j % 255;
                                }

                                shared_memory[i].swnd.start_sequence = j;

                                printf("Thread R: Remote window size for socket %d is %d\n\n", i, remote_window_size);
                            }
                        }

                        // It's a data message
                        else if (!dropMessage(P))
                        {
                            int received_seq_num = buffer[0];

                            if (received_seq_num < 0 || received_seq_num > 255)
                            {
                                continue;
                            }

                            if (shared_memory[i].rwnd.start_sequence == received_seq_num && shared_memory[i].rwnd.size > 0)
                            {
                                printf("\n2. Received Seq Num = %d\n", received_seq_num);
                                printf("2. Shared_memory[i].rwnd.start_sequence = %d\n", shared_memory[i].rwnd.start_sequence);
                                printf("2. Shared_memory[i].rwnd.size = %d\n", shared_memory[i].rwnd.size);

                                if (received_seq_num <= 0 || received_seq_num > 255)
                                {
                                    continue;
                                }
                                for (int j = 0; j < 10; j++)
                                {
                                    if (shared_memory[i].recv_buffer[j][0] == received_seq_num)
                                    {
                                        // Send ACK signal
                                        unsigned char ack_buffer[6];
                                        ack_buffer[0] = received_seq_num;
                                        ack_buffer[1] = shared_memory[i].rwnd.size;
                                        ack_buffer[2] = 'A';
                                        ack_buffer[3] = 'C';
                                        ack_buffer[4] = 'K';
                                        ack_buffer[5] = '#';
                                        struct sockaddr_in dest_addr;
                                        memset(&dest_addr, 0, sizeof(dest_addr));
                                        dest_addr.sin_family = AF_INET;
                                        dest_addr.sin_port = htons(shared_memory[i].dest_port);

                                        dest_addr.sin_addr.s_addr = inet_addr(shared_memory[i].dest_ip);

                                        sendto(shared_memory[i].udp_socket, ack_buffer, sizeof(ack_buffer), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                                        printf("Thread R: Sent the already received data ACK for seq_num %d\n", received_seq_num);
                                        break;
                                    }
                                }

                                shared_memory[i].last_ack_received = received_seq_num;
                                // Store the message in the receive buffer/ window
                                for (int j = 0; j < 10; j++)
                                {
                                    if (shared_memory[i].rbuff_free[j])
                                    {
                                        memcpy(shared_memory[i].recv_buffer[j], buffer, sizeof(buffer));
                                        shared_memory[i].rwnd.index_seq_num[received_seq_num] = j;
                                        shared_memory[i].rbuff_free[j] = false;
                                        break;
                                    }
                                }

                                shared_memory[i].rwnd.size--;

                                if (shared_memory[i].rwnd.size == 0)
                                {
                                    shared_memory[i].no_space_flags = 1;
                                }

                                unsigned char ack_buffer[6];
                                ack_buffer[0] = received_seq_num;
                                ack_buffer[1] = shared_memory[i].rwnd.size;
                                ack_buffer[2] = 'A';
                                ack_buffer[3] = 'C';
                                ack_buffer[4] = 'K';
                                ack_buffer[5] = '#';

                                struct sockaddr_in dest_addr;
                                memset(&dest_addr, 0, sizeof(dest_addr));
                                dest_addr.sin_family = AF_INET;
                                dest_addr.sin_port = htons(shared_memory[i].dest_port);

                                dest_addr.sin_addr.s_addr = inet_addr(shared_memory[i].dest_ip);

                                sendto(shared_memory[i].udp_socket, ack_buffer, sizeof(ack_buffer), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

                                shared_memory[i].rwnd.valid_seq_num[received_seq_num] = 0;

                                while (shared_memory[i].rwnd.valid_seq_num[shared_memory[i].rwnd.start_sequence] == 0)
                                {
                                    shared_memory[i].rwnd.start_sequence = 1 + shared_memory[i].rwnd.start_sequence % 255;
                                }
                            }

                            // Out-of-order packet but expected
                            else if (received_seq_num % 255 + 1 > shared_memory[i].rwnd.start_sequence && received_seq_num <= (shared_memory[i].rwnd.start_sequence + 9 - shared_memory[i].rwnd.size) % 255 + 1)
                            {

                                bool back_acknowledged = true;

                                for (int j = shared_memory[i].rwnd.start_sequence; j != received_seq_num; j = 1 + j % 255)
                                {
                                    if (shared_memory[i].rwnd.valid_seq_num[j] == 1)
                                    {
                                        back_acknowledged = false;

                                        break;
                                    }
                                }
                                // if all aren't back acknowledged and the current one is received first time, then store it in the buffer
                                // no ACK signal is sent in this case

                                if (back_acknowledged != true)
                                {
                                    printf("\nThread R: Current Seq Num = %d, all back are not acknowledged\n", received_seq_num);

                                    if (shared_memory[i].rwnd.valid_seq_num[received_seq_num] == 1)
                                    {
                                        // Copy in buffer
                                        int start_sequence_index = shared_memory[i].rwnd.index_seq_num[shared_memory[i].rwnd.start_sequence];
                                        int diff = (received_seq_num - shared_memory[i].rwnd.start_sequence + 255) % 255;
                                        int index = (start_sequence_index + diff) % 10;
                                        if (shared_memory[i].rbuff_free[index] == 0)
                                        {
                                            continue;
                                        }
                                        printf("Storing it in index = %d\n\n", index);
                                        memcpy(shared_memory[i].recv_buffer[index], buffer, sizeof(buffer));
                                        shared_memory[i].rwnd.index_seq_num[received_seq_num] = index;
                                        shared_memory[i].rbuff_free[index] = false;

                                        shared_memory[i].rwnd.size--;
                                        if (shared_memory[i].rwnd.size == 0)
                                        {
                                            shared_memory[i].no_space_flags = 1;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                // Out-of-order packet and not expected
                                printf("Thread R: Out of order and not expected message on socket %d, seq_num %d\n", i, buffer[0]);
                                printf("Thread R: Shared_memory[i].rwnd.start_sequence = %d\n", shared_memory[i].rwnd.start_sequence);
                                printf("Thread R: Shared_memory[i].rwnd.size = %d\n", shared_memory[i].rwnd.size);
                            }
                        }
                        else
                        {
                            // Simulated packet loss
                            printf("\nThread R: Dropped message on socket %d, seq_num %d\n\n", i, buffer[0]);
                        }
                    }
                }
            }
            sem_post(sem_SM);
        }
        else if (activity == 0)
        {
            // Timeout occurred - already handled by checking nospace flags above
            printf("Thread R: Timeout occured\n");
        }
        else
        {
            perror("select() error");
        }
    }

    sem_close(sem_SM);
    return NULL;
}

// Thread S: Handles retransmissions and timeouts
void *thread_S_function(void *arg)
{
    printf("Thread S started\n"); // Add this to verify thread is running
    sem_SM = sem_open("sem_SM", 0);

    if (sem_SM == SEM_FAILED)
    {
        perror("k_socket: Failed to open sem_SM");
        return NULL;
    }

    while (1)
    {
        // Sleep for T/2 seconds
        sleep(T / 2);
        sem_wait(sem_SM);

        // Check each socket
        for (int i = 0; i < MAX_KTP_SOCKETS; i++)
        {
            if (shared_memory[i].is_free == 0)
            {
                bool timeout = false;
                for (int j = 0; j < 10; j++)
                {
                    if ((time(NULL) - shared_memory[i].send_time[j] >= T) && shared_memory[i].send_time[j] != -1)
                    {
                        printf("\nThread S: Timeout occurred on socket %d\n", i);
                        timeout = true;
                        int k = shared_memory[i].swnd.start_sequence;
                        int start = k;
                        int cnt = 0;
                        if (shared_memory[i].swnd.size == 0)
                        {
                            continue;
                        }
                        for (; k != (start + shared_memory[i].swnd.size) % 255 + 1 && cnt != shared_memory[i].swnd.size; k = (k) % 255 + 1)
                        {
                            int index = shared_memory[i].swnd.index_seq_num[k];
                            printf("\nThread S: index of retransmission = %d\n", index);
                            // Retransmit the message
                            struct sockaddr_in dest_addr;
                            memset(&dest_addr, 0, sizeof(dest_addr));
                            dest_addr.sin_family = AF_INET;
                            dest_addr.sin_port = htons(shared_memory[i].dest_port);
                            dest_addr.sin_addr.s_addr = inet_addr(shared_memory[i].dest_ip);

                            printf("Thread S: Retransmitted seq_num %d on socket %d\n", k, i);
                            printf("Because there time out = %ld\n", time(NULL) - shared_memory[i].send_time[index]);
                            shared_memory[i].send_time[index] = time(NULL);
                            printf("Shared_memory[i].udp_socket = %d\n\n", shared_memory[i].udp_socket);

                            sendto(shared_memory[i].udp_socket, shared_memory[i].send_buffer[index], MESSAGE_SIZE, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                            cnt++;
                        }
                        break;
                    }
                }

                if (!timeout)
                {
                    printf("Thread S: No timeout on socket %d\n", i);

                    int k = shared_memory[i].swnd.start_sequence;
                    int start = k;

                    if (shared_memory[i].swnd.size == 0)
                    {
                        continue;
                    }

                    int cnt = 0;
                    for (; k != (start + shared_memory[i].swnd.size) % 255 + 1 && cnt != shared_memory[i].swnd.size; k = (k) % 255 + 1)
                    {
                        if (shared_memory[i].swnd.index_seq_num[k] >= 0 && shared_memory[i].send_time[shared_memory[i].swnd.index_seq_num[k]] == -1)
                        {
                            // Send the message
                            struct sockaddr_in dest_addr;
                            memset(&dest_addr, 0, sizeof(dest_addr));
                            dest_addr.sin_family = AF_INET;
                            dest_addr.sin_port = htons(shared_memory[i].dest_port);
                            dest_addr.sin_addr.s_addr = inet_addr(shared_memory[i].dest_ip);

                            shared_memory[i].send_time[shared_memory[i].swnd.index_seq_num[k]] = time(NULL);

                            sendto(shared_memory[i].udp_socket, shared_memory[i].send_buffer[shared_memory[i].swnd.index_seq_num[k]], MESSAGE_SIZE, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                            printf("\nThread S: Sent seq_num %d on socket %d\n", k, i);
                            cnt++;
                        }
                    }
                }
            }
        }
        sem_post(sem_SM);

        // Read ACK message from thread R, then see the receiving window size, then send those much data to thread R
    }
    sem_close(sem_SM);
}

// Initialize KTP socket system
int init_ksocket()
{
    sem_SM = sem_open("sem_SM", 0); // Initialize with count 0
    if (sem_SM == SEM_FAILED)
    {
        perror("Failed to create semaphore sem2");
        return EXIT_FAILURE;
    }

    sem_wait(sem_SM);

    shared_memory = NULL;
    size_t shared_mem_size = MAX_KTP_SOCKETS * sizeof(SharedMemoryEntry);
    printf("Requesting shared memory of size: %zu bytes (%.2f MB)\n", shared_mem_size, shared_mem_size / (1024.0 * 1024.0));
    // Create shared memory for KTP sockets
    int shmid = shmget(SHM_KEY, shared_mem_size, IPC_CREAT | 0666);
    if (shmid < 0)
    {
        perror("Failed to create shared memory");
        return -1;
    }

    // Attach to the shared memory segment
    shared_memory = (SharedMemoryEntry *)shmat(shmid, NULL, 0);
    if (shared_memory == (void *)-1)
    {
        perror("Failed to attach to shared memory");
        return -1;
    }
    sem_post(sem_SM);

    // Now initialize shared memory entries
    sem_wait(sem_SM);
    for (int i = 0; i < MAX_KTP_SOCKETS; i++)
    {

        // Initialize the shared memory entry
        shared_memory[i].is_free = 1;
        shared_memory[i].pid = 0;
        shared_memory[i].udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        shared_memory[i].dest_ip[0] = '\0';
        shared_memory[i].dest_port = 0;
        shared_memory[i].src_ip[0] = '\0';
        shared_memory[i].src_port = 0;
        shared_memory[i].swnd.size = 10;
        shared_memory[i].rwnd.size = 10;
        shared_memory[i].max_seq_number_yet = 1;
        shared_memory[i].swnd.start_sequence = 1;
        shared_memory[i].rwnd.start_sequence = 1;
        shared_memory[i].last_ack_received = 0;
        shared_memory[i].no_space_flags = 0;

        // Initialize arrays with memset
        memset(shared_memory[i].swnd.valid_seq_num, 1, sizeof(shared_memory[i].swnd.valid_seq_num));
        memset(shared_memory[i].rwnd.valid_seq_num, 1, sizeof(shared_memory[i].rwnd.valid_seq_num));
        memset(shared_memory[i].swnd.index_seq_num, -1, sizeof(shared_memory[i].swnd.index_seq_num));
        memset(shared_memory[i].rwnd.index_seq_num, -1, sizeof(shared_memory[i].rwnd.index_seq_num));
        memset(shared_memory[i].send_time, -1, sizeof(shared_memory[i].send_time));
        memset(shared_memory[i].recv_buffer[0], 0, sizeof(shared_memory[i].recv_buffer));
        memset(shared_memory[i].send_buffer[0], 0, sizeof(shared_memory[i].send_buffer));
        memset(shared_memory[i].rbuff_free, 1, sizeof(shared_memory[i].rbuff_free));
        memset(shared_memory[i].sbuff_free, 1, sizeof(shared_memory[i].sbuff_free));

        // Unlock the mutex
    }
    sem_post(sem_SM);
    sem_close(sem_SM);

    printf("KTP socket system initialized successfully.\n");
    return 0;
}

// Clean up resources when program exits
void cleanup_resources()
{
    sem_unlink("sem1");
    sem_unlink("sem2");
    sem_unlink("sem_SM");

    // Detach shared memory
    if (shared_memory != NULL && shared_memory != (void *)-1)
    {
        shmdt(shared_memory);
    }
}

// Main function to initialize KTP socket system
int main()
{
    // Set up cleanup handler
    atexit(cleanup_resources);
    // Create and initialize the semaphores first
    sem_unlink("sem1");
    sem_unlink("sem2");
    sem_unlink("sem_SM");

    sem1 = sem_open("sem1", O_CREAT, 0666, 0);
    if (sem1 == SEM_FAILED)
    {
        perror("Failed to create semaphore sem1");
        return EXIT_FAILURE;
    }

    sem2 = sem_open("sem2", O_CREAT, 0666, 0);
    if (sem2 == SEM_FAILED)
    {
        perror("Failed to create semaphore sem2");
        sem_close(sem1);
        sem_unlink("sem1");
        return EXIT_FAILURE;
    }

    sem_SM = sem_open("sem_SM", O_CREAT, 0666, 1);
    if (sem_SM == SEM_FAILED)
    {
        perror("Failed to create semaphore sem2");
        sem_close(sem1);
        sem_close(sem2);
        sem_unlink("sem1");
        sem_unlink("sem2");
        return EXIT_FAILURE;
    }

    // Close the semaphores - programs will reopen them as needed
    sem_close(sem1);
    sem_close(sem2);
    sem_close(sem_SM);

    // Seed random number generator for dropMessage()
    srand(time(NULL));

    if (init_ksocket() != 0)
    {
        fprintf(stderr, "Failed to initialize KTP socket system.\n");
        sem_unlink("sem1");
        sem_unlink("sem2");
        sem_unlink("sem_SM");
        return EXIT_FAILURE;
    }

    printf("Out of init_ksocket\n");

    pthread_t S_thread, R_thread, G_thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&S_thread, &attr, thread_S_function, NULL);
    pthread_create(&R_thread, &attr, thread_R_function, NULL);
    pthread_create(&G_thread, &attr, garbage_collector, NULL);

    sem_t *sem1 = sem_open("sem1", 0);
    sem_t *sem2 = sem_open("sem2", 0);
    sem_t *sem_SM = sem_open("sem_SM", 0);
    if (sem1 == SEM_FAILED || sem2 == SEM_FAILED || sem_SM == SEM_FAILED)
    {
        perror("bind_function: Failed to open semaphores");
        return 0;
    }

    printf("semaphores opened\n");

    while (1)
    { // Make this a continuous loop
        printf("bind_function: Waiting on sem2\n");
        sem_wait(sem2); // Wait for signal from k_bind
        printf("bind_function: Running bind operation\n");

        for (int i = 0; i < 10; i++)
        {
            if (shared_memory[i].is_free == 2)
            {
                sem_wait(sem_SM);

                struct sockaddr_in server_addr;
                memset(&server_addr, 0, sizeof(server_addr));
                server_addr.sin_family = AF_INET;
                server_addr.sin_port = htons(shared_memory[i].src_port);
                server_addr.sin_addr.s_addr = inet_addr(shared_memory[i].src_ip);

                // bind the socket
                if (bind(shared_memory[i].udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                {
                    perror("bind failed");
                }
                shared_memory[i].is_free = 0; // Mark as bound
                sem_post(sem_SM);
                break;
            }
        }

        // Release sem1 to signal k_bind that binding is complete
        printf("bind_function: Releasing sem1\n");
        sem_post(sem1);
    }

    // Clean up semaphores at the end
    sem_unlink("sem1");
    sem_unlink("sem2");
    sem_unlink("sem_SM");

    return EXIT_SUCCESS;
}
