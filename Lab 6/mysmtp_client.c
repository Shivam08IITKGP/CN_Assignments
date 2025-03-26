/*=====================================
Name: Shivam Choudhury
Roll number: 22CS10072
=====================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096
#define MAX_EMAIL_SIZE 4096

int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in server_addr;
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    // Check command line arguments
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // Initialize server address struct
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Convert IP address from string to binary form
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Connect to server
    connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    printf("Connected to My_SMTP server.\n");

    int running = 1;
    while (running)
    {
        printf("> ");
        if (fgets(command, BUFFER_SIZE, stdin) == NULL)
        {
            break;
        }

        // Remove trailing newline
        command[strcspn(command, "\n")] = 0;

        // Check if command is empty
        if (strlen(command) == 0)
        {
            continue;
        }

        // Handle DATA command specially
        if (strncmp(command, "DATA", 4) == 0)
        {
            // Send DATA command
            strcat(command, "\r\n");
            send(sockfd, command, strlen(command), 0);

            // Receive server response
            memset(response, 0, BUFFER_SIZE);
            recv(sockfd, response, BUFFER_SIZE, 0);
            printf("%s", response);

            char email_body[MAX_EMAIL_SIZE] = "";
            char line[BUFFER_SIZE];

            while (1)
            {
                if (fgets(line, BUFFER_SIZE, stdin) == NULL)
                {
                    break;
                }

                // Remove trailing newline
                line[strcspn(line, "\n")] = 0;

                // Check for end of data
                if (strcmp(line, ".") == 0)
                {
                    strcat(email_body, ".\r\n");
                    send(sockfd, email_body, strlen(email_body), 0);
                    break;
                }

                // Append line to email body
                strcat(line, "\r\n");
                strcat(email_body, line);
            }

            // Get server response after data
            memset(response, 0, BUFFER_SIZE);
            recv(sockfd, response, BUFFER_SIZE, 0);
            printf("%s", response);

            continue;
        }

        // Regular command handling
        strcat(command, "\r\n");
        send(sockfd, command, strlen(command), 0);

        // Wait for and display server response
        memset(response, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(sockfd, response, BUFFER_SIZE - 1, 0);

        if (bytes_received <= 0)
        {
            printf("Server disconnected\n");
            break;
        }

        response[bytes_received] = '\0';
        printf("%s", response);

        // Check if QUIT command was sent
        if (strncmp(command, "QUIT", 4) == 0)
        {
            running = 0;
        }
    }

    close(sockfd);
    return 0;
}