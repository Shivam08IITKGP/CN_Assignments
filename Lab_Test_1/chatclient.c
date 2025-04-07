#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <ctype.h>

#define BUFFER_SIZE 1024

int main()
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5000);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    // Get client address info
    getsockname(sockfd, (struct sockaddr *)&client_addr, &len);
    printf("Connected to server as %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    fd_set readfds;
    int maxfd = (sockfd > STDIN_FILENO) ? sockfd : STDIN_FILENO;

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("Select error");
            exit(EXIT_FAILURE);
        }

        // Handle user input
        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            memset(buffer, 0, BUFFER_SIZE);
            if (!fgets(buffer, BUFFER_SIZE, stdin))
                continue;
            buffer[strcspn(buffer, "\n")] = 0;

            // Validate numeric input
            bool valid = true;
            for (int i = 0; buffer[i]; i++)
            {
                if (!isdigit(buffer[i]))
                {
                    valid = false;
                    break;
                }
            }

            if (valid)
            {
                send(sockfd, buffer, strlen(buffer), 0);
                printf("Client %s:%d Number %s sent to server\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), buffer);
            }
        }

        // Handle server messages
        if (FD_ISSET(sockfd, &readfds))
        {
            memset(buffer, 0, BUFFER_SIZE);
            int valread = recv(sockfd, buffer, BUFFER_SIZE, 0);
            if (valread <= 0)
            {
                printf("Server disconnected\n");
                break;
            }
            printf("Client: Received the following message text from the server: \n%s\n", buffer);
        }
    }

    close(sockfd);
    return 0;
}
