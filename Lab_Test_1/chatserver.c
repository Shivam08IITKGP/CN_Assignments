#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <limits.h>
#include <unistd.h>

#define MAX_CLIENTS 5
#define BUFFER_SIZE 1024

int main()
{
    int serverfd, clientsockfd[MAX_CLIENTS];
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    fd_set readfds, tempfds;
    int maxfd, activity, i, valread;
    int numclient = 0;
    int round = 1;
    char buffer[BUFFER_SIZE];

    int client_sent[MAX_CLIENTS] = {0};
    int client_numbers[MAX_CLIENTS] = {0};

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        clientsockfd[i] = 0;
    }

    // Create server socket
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(5000);

    // Bind socket
    if (bind(serverfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(serverfd, 5) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started. Waiting for connections...\n");

    FD_ZERO(&readfds);
    FD_SET(serverfd, &readfds);
    maxfd = serverfd;

    while (1)
    {
        tempfds = readfds;
        activity = select(maxfd + 1, &tempfds, NULL, NULL, NULL);

        if (activity < 0)
        {
            perror("Select error");
            exit(EXIT_FAILURE);
        }

        // Handle new connections
        if (FD_ISSET(serverfd, &tempfds))
        {
            addr_size = sizeof(client_addr);
            int new_socket = accept(serverfd, (struct sockaddr *)&client_addr, &addr_size);

            printf("Server: Received a new connection from client %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            if (numclient < MAX_CLIENTS)
            {
                clientsockfd[numclient] = new_socket;
                FD_SET(new_socket, &readfds);
                if (new_socket > maxfd)
                    maxfd = new_socket;
                numclient++;

                // Send initial message if minimum clients reached
                if (numclient >= 2)
                {
                    snprintf(buffer, BUFFER_SIZE, "Server: Send your number for Round %d", round);
                    if (numclient == 2)
                        for (int j = 0; j < numclient; j++)
                        {
                            send(clientsockfd[j], buffer, strlen(buffer), 0);
                        }
                    else
                    {
                        send(clientsockfd[numclient - 1], buffer, strlen(buffer), 0);
                    }
                }
            }
            else
            {
                close(new_socket);
            }
        }

        // Handle client messages
        for (i = 0; i < numclient; i++)
        {
            if (FD_ISSET(clientsockfd[i], &tempfds))
            {
                memset(buffer, 0, BUFFER_SIZE);
                valread = read(clientsockfd[i], buffer, BUFFER_SIZE);

                if (valread <= 0)
                {
                    // Handle client disconnect
                    getpeername(clientsockfd[i], (struct sockaddr *)&client_addr, &addr_size);
                    printf("Client %s:%d disconnected\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                    close(clientsockfd[i]);
                    FD_CLR(clientsockfd[i], &readfds);

                    // Shift remaining clients
                    for (int j = i; j < numclient - 1; j++)
                    {
                        clientsockfd[j] = clientsockfd[j + 1];
                        client_sent[j] = client_sent[j + 1];
                        client_numbers[j] = client_numbers[j + 1];
                    }
                    numclient--;
                    i--;
                }
                else
                {
                    if (numclient < 2)
                    {
                        getpeername(clientsockfd[i], (struct sockaddr *)&client_addr, &addr_size);
                        printf("Server: Insufficient clients, \"%s\" from client %s:%d dropped\n", buffer, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    }
                    else
                    {
                        if (client_sent[i])
                        {
                            snprintf(buffer, BUFFER_SIZE, "Server: Duplicate messages for Round %d are not allowed. Please wait for the results for Round %d and Call for the number for Round %d.", round, round, round + 1);
                            send(clientsockfd[i], buffer, strlen(buffer), 0);
                        }
                        else
                        {
                            // Store the number
                            printf("Server: Received number %s from client %s:%d\n", buffer, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                            client_numbers[i] = atoi(buffer);
                            client_sent[i] = 1;

                            // Check if all clients have sent numbers
                            int all_sent = 1;
                            for (int j = 0; j < numclient; j++)
                            {
                                if (!client_sent[j])
                                {
                                    all_sent = 0;
                                    break;
                                }
                            }

                            if (all_sent)
                            {
                                // Find maximum number
                                int max_num = INT_MIN;
                                int max_index = 0;
                                for (int j = 0; j < numclient; j++)
                                {
                                    if (client_numbers[j] > max_num)
                                    {
                                        max_num = client_numbers[j];
                                        max_index = j;
                                    }
                                }

                                // Get winner's address
                                struct sockaddr_in winner_addr;
                                socklen_t len = sizeof(winner_addr);
                                getpeername(clientsockfd[max_index], (struct sockaddr *)&winner_addr, &len);

                                // Prepare message
                                snprintf(buffer, BUFFER_SIZE,
                                         "Server: Maximum Number Received in Round %d is: %d. The number has been received from the client %s:%d\n"
                                         "Server: Enter the number for Round %d:",
                                         round, max_num, inet_ntoa(winner_addr.sin_addr), ntohs(winner_addr.sin_port), round + 1);

                                // Broadcast results
                                for (int j = 0; j < numclient; j++)
                                {
                                    send(clientsockfd[j], buffer, strlen(buffer), 0);
                                    client_sent[j] = 0;
                                }

                                round++;
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}
