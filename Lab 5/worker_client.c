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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>

#define PORT 8080
#define MAX_BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1" // Change to server IP if running on different machines

double perform_operation(int num1, char op, int num2)
{
    switch (op)
    {
    case '+':
        return num1 + num2;
    case '-':
        return num1 - num2;
    case '*':
        return num1 * num2;
    case '/':
        if (num2 == 0)
        {
            printf("Division by zero error\n");
            return INT_MAX;
        }
        return (double)num1 / num2;
    default:
        return 0;
    }
}

int main(int argc, char *argv[])
{
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[MAX_BUFFER_SIZE] = {0};
    char result_msg[MAX_BUFFER_SIZE] = {0};
    int num_tasks = 0;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);

    // Set up server address structure

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);
    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    printf("Connected to Task Queue Server.\n");

    // Process tasks until no more are available
    while (1)
    {
        // Request a task
        printf("Requesting a task...\n");
        strcpy(buffer, "GET_TASK");
        send(sock, buffer, strlen(buffer), 0);

        // Receive server response
        memset(buffer, 0, sizeof(buffer));
        int bytes_read = read(sock, buffer, sizeof(buffer));
        if (bytes_read <= 0)
        {
            printf("Server disconnected\n");
            // deassign the tasks assigned to the client

            break;
        }

        printf("Server response: %s\n", buffer);

        // Check if no tasks are available
        if (strcmp(buffer, "No tasks available") == 0)
        {
            printf("No more tasks available. Exiting...\n");
            break;
        }

        // Check if there's an error with the task request
        if (strncmp(buffer, "Error:", 6) == 0)
        {
            printf("Already one task is assigned\n");
            printf("%s\n", buffer);
            // Sleep for a bit before requesting another task
            sleep(2);
            continue;
        }

        if (strncmp(buffer, "Is", 2) == 0)
        {
            printf("Already one task is assigned\n");
            printf("%s\n", buffer);
            // Sleep for a bit before requesting another task
            sleep(2);
            continue;
        }

        // Parse the task if valid
        if (strncmp(buffer, "Task:", 5) == 0)
        {
            char task_str[MAX_BUFFER_SIZE];
            strcpy(task_str, buffer + 6);

            int num1, num2;
            char op;

            // Parse the arithmetic operation
            if (sscanf(task_str, "%d %c %d", &num1, &op, &num2) == 3)
            {
                // Perform the operation
                double result = perform_operation(num1, op, num2);

                // Send the result back to the server
                sprintf(result_msg, "RESULT %.2f", result);
                printf("Sending result: %s\n", result_msg);
                send(sock, result_msg, strlen(result_msg), 0);

                // Wait for server acknowledgment
                memset(buffer, 0, sizeof(buffer));
                bytes_read = read(sock, buffer, sizeof(buffer));
                if (bytes_read <= 0)
                {
                    printf("Server disconnected\n");
                    break;
                }

                printf("Server acknowledgment: %s\n", buffer);
                num_tasks++;

                // Simulate computation time
                sleep(1);
            }
            else
            {
                printf("Invalid task format: %s\n", task_str);
            }
        }
    }

    // Send exit message to server before closing
    strcpy(buffer, "exit");
    send(sock, buffer, strlen(buffer), 0);

    // Close the socket
    close(sock);

    printf("Completed %d tasks. Worker client exiting.\n", num_tasks);
    return 0;
}