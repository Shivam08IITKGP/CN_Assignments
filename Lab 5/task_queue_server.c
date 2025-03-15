/**
=====================================
Shivam Choudhury
22CS10072
=====================================
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <limits.h>
#include <sys/shm.h>

#define PORT 8080
#define MAX_TASKS 100
#define MAX_TASK_LEN 50
#define MAX_CLIENTS 10

// Structure to represent a task
typedef struct
{
    char description[MAX_TASK_LEN];
    int state;
    // 0: not assigned
    // 1: assigned but not completed
    // 2: assigned and completed
    int assigned_to_client;
    // client ID to which task is assigned
} Task;

// Structure for shared memory
typedef struct
{
    Task tasks[MAX_TASKS];
    int task_count;
    int next_task_index;
    sem_t mutex;
    int server_shutdown;
} SharedData;

int shmid;
SharedData *shared_data;

// Function to handle zombie processes
void handle_sigchld(int sig)
{
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved_errno;
}

// Function to load tasks from a config file
int load_tasks(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Error opening config file");
        return -1;
    }

    char line[MAX_TASK_LEN];
    int count = 0;

    while (fgets(line, sizeof(line), file) && count < MAX_TASKS)
    {
        // Remove newline character if present
        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
        {
            line[len - 1] = '\0';
        }

        strcpy(shared_data->tasks[count].description, line);
        shared_data->tasks[count].state = 0;
        shared_data->tasks[count].assigned_to_client = -1;
        count++;
    }

    shared_data->task_count = count;
    shared_data->next_task_index = 0;
    fclose(file);

    printf("Loaded %d tasks from config file\n", count);
    return count;
}

// Function to get next available task
int get_next_task(int client_id)
{
    sem_wait(&shared_data->mutex);

    // Check if this client already has an assigned task
    for (int i = 0; i < shared_data->task_count; i++)
    {
        if (shared_data->tasks[i].state == 1 && shared_data->tasks[i].assigned_to_client == client_id)
        {
            sem_post(&shared_data->mutex);
            return -2; // Client already has an assigned task
        }
    }

    // Find next available task
    for (int i = 0; i < shared_data->task_count; i++)
    {
        if (shared_data->tasks[i].state == 0)
        {
            shared_data->tasks[i].state = 1;
            shared_data->tasks[i].assigned_to_client = client_id;
            sem_post(&shared_data->mutex);
            return i;
        }
    }

    sem_post(&shared_data->mutex);
    return -1; // No tasks available
}

// Function to mark task as completed
void complete_task(int task_index)
{
    sem_wait(&shared_data->mutex);
    if (task_index >= 0 && task_index < shared_data->task_count)
    {
        shared_data->tasks[task_index].state = 2;
    }
    sem_post(&shared_data->mutex);
}

// Function to clear assigned tasks to a client
void clear_assigned_tasks(int client_id)
{
    sem_wait(&shared_data->mutex);
    for (int i = 0; i < shared_data->task_count; i++)
    {
        if (shared_data->tasks[i].assigned_to_client == client_id)
        {
            shared_data->tasks[i].state = 0;
            shared_data->tasks[i].assigned_to_client = -1;
        }
    }
    sem_post(&shared_data->mutex);
}

// Function to check if all tasks are completed
int all_tasks_completed()
{
    sem_wait(&shared_data->mutex);

    int all_completed = 1; // Assume all are completed
    for (int i = 0; i < shared_data->task_count; i++)
    {
        // If any task is not completed yet, return false
        if (shared_data->tasks[i].state != 2)
        {
            all_completed = 0;
            break;
        }
    }

    sem_post(&shared_data->mutex);
    return all_completed;
}

// Function to handle client communication
void handle_client(int client_sock, int client_id)
{
    char buffer[1024];
    char task_msg[1024];
    ssize_t bytes_received;
    int current_task = -1;

    // Set socket to non-blocking mode
    int flags = fcntl(client_sock, F_GETFL, 0);
    fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);

    printf("Client %d connected\n", client_id);

    while (1)
    {
        if (shared_data->server_shutdown)
        {
            printf("Child process %d detected server shutdown, notifying client and exiting\n", client_id);
            strcpy(task_msg, "Server shutting down");
            send(client_sock, task_msg, strlen(task_msg), 0);

            sleep(1);
            break;
        }

        // Non-blocking receive
        bytes_received = recv(client_sock, buffer, sizeof(buffer), 0);

        if (bytes_received > 0)
        {
            buffer[bytes_received] = '\0';
            printf("Client %d sent: %s\n", client_id, buffer);

            // Check for exit command
            if (strncmp(buffer, "exit", 4) == 0)
            {
                printf("Client %d disconnecting\n", client_id);
                break;
            }

            // Check for GET_TASK command
            else if (strncmp(buffer, "GET_TASK", 8) == 0)
            {
                int task_index = get_next_task(client_id);

                if (task_index == -2)
                {
                    strcpy(task_msg, "Error: You already have an assigned task. Complete it first.");
                    send(client_sock, task_msg, strlen(task_msg), 0);
                }
                else if (task_index >= 0)
                {
                    sprintf(task_msg, "Task: %s", shared_data->tasks[task_index].description);

                    send(client_sock, task_msg, strlen(task_msg), 0);
                    current_task = task_index;
                    printf("Task assigned to client %d: %s\n", client_id, shared_data->tasks[task_index].description);
                }
                else
                {
                    strcpy(task_msg, "No tasks available");
                    send(client_sock, task_msg, strlen(task_msg), 0);
                }
            }

            // Check for RESULT command
            else if (strncmp(buffer, "RESULT", 6) == 0)
            {
                if (current_task >= 0)
                {
                    complete_task(current_task);
                    double result = atof(buffer + 7);

                    if (result == INT_MAX)
                    {
                        printf("Task completed by client %d: %s, Result: Division by Zero Error\n", client_id, shared_data->tasks[current_task].description);
                        current_task = -1;
                    }
                    else
                    {
                        printf("Task completed by client %d: %s, Result: %s\n", client_id, shared_data->tasks[current_task].description, buffer + 6);
                        current_task = -1;
                    }
                    // Acknowledge result
                    strcpy(task_msg, "Result received");
                    send(client_sock, task_msg, strlen(task_msg), 0);
                }
            }
        }

        else if (bytes_received == 0)
        {
            // Client disconnected
            printf("Client %d disconnected\n", client_id);
            printf("Clearing all its assigned tasks and not completed tasks\n");

            // Clear the assigned tasks to this client in the shared_memory
            clear_assigned_tasks(client_id);
            break;
        }
        else if (bytes_received < 0)
        {
            // Would block, do nothing
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Sleep a bit to avoid CPU hogging
                usleep(100000); // 0.1 seconds
            }
            else
            {
                perror("recv failed");
                break;
            }
        }
    }

    close(client_sock);
    exit(0);
}

// Initialize shared memory and semaphore
void init_shared_memory()
{
    // Create shared memory segment
    shmid = shmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0666);
    if (shmid < 0)
    {
        perror("shmget failed");
        exit(1);
    }

    // Attach shared memory segment
    shared_data = (SharedData *)shmat(shmid, NULL, 0);
    if (shared_data == (void *)-1)
    {
        perror("shmat failed");
        exit(1);
    }

    // Initialize semaphore
    if (sem_init(&shared_data->mutex, 1, 1) == -1)
    {
        perror("sem_init failed");
        exit(1);
    }

    memset(shared_data->tasks, 0, sizeof(shared_data->tasks));
    shared_data->task_count = 0;
    shared_data->next_task_index = 0;
    shared_data->server_shutdown = 0;
}

// Cleanup shared memory
void cleanup_shared_memory()
{
    // Destroy semaphore
    sem_destroy(&shared_data->mutex);

    // Detach shared memory
    shmdt(shared_data);

    // Remove shared memory segment
    shmctl(shmid, IPC_RMID, NULL);
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <task_config_file>\n", argv[0]);
        return 1;
    }

    // Setup signal handler for SIGCHLD
    if (signal(SIGCHLD, handle_sigchld) == SIG_ERR)
    {
        perror("signal handler registration failed");
        return 1;
    }

    // Initialize shared memory
    init_shared_memory();

    // Load tasks from config file
    if (load_tasks(argv[1]) <= 0)
    {
        fprintf(stderr, "No tasks loaded or error loading tasks.\n");
        cleanup_shared_memory();
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;

    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in address;
    int addrlen = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);

    printf("Task Queue Server started. Listening on port %d...\n", PORT);

    int client_id = 0;

    while (1)
    {
        if (all_tasks_completed() && shared_data->task_count > 0)
        {
            printf("All tasks completed. Shutting down server.\n");

            // Set the shutdown flag to notify child processes
            sem_wait(&shared_data->mutex);
            shared_data->server_shutdown = 1;
            sem_post(&shared_data->mutex);

            // Give child processes time to detect the shutdown flag
            sleep(2);

            break; // Exit the loop
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 0.1 seconds

        int activity = select(server_fd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0)
        {
            if (errno == EINTR)
            {
                // Interrupted by signal, continue and check again
                continue;
            }
            else
            {
                perror("select failed");
                break;
            }
        }

        // Accept a new connection
        int client_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);

        if (client_sock < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Would block, continue and try again
                continue;
            }
            else
            {
                perror("accept failed");
                continue;
            }
        }

        // Fork a new process to handle this client
        pid_t child_pid = fork();

        if (child_pid < 0)
        {
            perror("fork failed");
            close(client_sock);
        }
        else if (child_pid == 0)
        {
            // Child process
            close(server_fd);
            // Close the listening socket in the child

            handle_client(client_sock, client_id);
            exit(0);
        }
        else
        {
            // Parent process
            close(client_sock);
            // Close the accepted socket in the parent
            client_id++;
        }
    }

    // Cleanup
    close(server_fd);
    cleanup_shared_memory();
    return 0;
}