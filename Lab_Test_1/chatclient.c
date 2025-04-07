#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>


#define BUFFER_SIZE 1024

int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    fd_set readfds, tempfds;
    int maxfd, activity;
    char buffer[BUFFER_SIZE];

    // Create client socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Error in socket creation");
        exit(1);
    }

    // Configure server address to connect to
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);

    // If server IP provided as argument, use it, otherwise use localhost
    if (argc > 1)
    {
        server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    }
    else
    {
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        exit(1);
    }

    // Get client address info
    addr_size = sizeof(client_addr);
    getsockname(sockfd, (struct sockaddr *)&client_addr, &addr_size);
    printf("Connected to server as %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // Initialize file descriptor set
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    FD_SET(STDIN_FILENO, &readfds); // Add STDIN to listen for user input

    maxfd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

    while (1)
    {
        // Copy file descriptor set
        tempfds = readfds;

        // Wait for activity on file descriptors
        activity = select(maxfd + 1, &tempfds, NULL, NULL, NULL);

        if (activity < 0)
        {
            perror("Select error");
            exit(1);
        }

        // Check for input from user
        if (FD_ISSET(STDIN_FILENO, &tempfds))
        {
            memset(buffer, 0, BUFFER_SIZE);
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL)
            {
                // Remove newline character
                buffer[strcspn(buffer, "\n")] = 0;

                // Send number to server
                send(sockfd, buffer, strlen(buffer), 0);

                // Print message
                printf("Client %s:%d Number %s sent to server\n",
                       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), buffer);
            }
        }

        // Check for message from server
        if (FD_ISSET(sockfd, &tempfds))
        {
            memset(buffer, 0, BUFFER_SIZE);
            int valread = read(sockfd, buffer, BUFFER_SIZE);

            if (valread <= 0)
            {
                // Server disconnected
                printf("Server disconnected\n");
                close(sockfd);
                exit(0);
            }
            else
            {
                // Print received message
                printf("Client: Received the following message text from the server: %s\n", buffer);
            }
        }
    }

    close(sockfd);
    return 0;
}
