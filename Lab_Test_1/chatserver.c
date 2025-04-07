#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_CLIENTS 5
#define BUFFER_SIZE 1024

int main() {
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
    
    for (i = 0; i < MAX_CLIENTS; i++) {
        clientsockfd[i] = 0;
    }
    
    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        perror("Error in socket creation");
        exit(1);
    }
    
    int opt = 1;
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(1);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);
    
    if (bind(serverfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    if (listen(serverfd, 5) < 0) {
        perror("Listen failed");
        exit(1);
    }
    
    printf("Server started. Waiting for connections...\n");
    
    FD_ZERO(&readfds);
    FD_SET(serverfd, &readfds);
    maxfd = serverfd;
    
    while(1) {
        tempfds = readfds;
        
        activity = select(maxfd + 1, &tempfds, NULL, NULL, NULL);
        
        if (activity < 0) {
            perror("Select error");
            exit(1);
        }
        
        if (FD_ISSET(serverfd, &tempfds)) {
            int new_socket;
            addr_size = sizeof(client_addr);
            new_socket = accept(serverfd, (struct sockaddr *)&client_addr, &addr_size);
            
            if (new_socket < 0) {
                perror("Accept failed");
                exit(1);
            }
            
            printf("Server: Received a new connection from client %s:%d\n", 
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            
            if (numclient < MAX_CLIENTS) {
                clientsockfd[numclient] = new_socket;
                numclient++;
                
                FD_SET(new_socket, &readfds);
                if (new_socket > maxfd) {
                    maxfd = new_socket;
                }
                
                // Fixed: Only send prompt when minimum clients are connected
                if (numclient >= 2) {
                    sprintf(buffer, "Server: Send your number for Round %d", round);
                    if (numclient == 2) {
                        // Send to all existing clients when reaching minimum threshold
                        for (int j = 0; j < numclient; j++) {
                            send(clientsockfd[j], buffer, strlen(buffer), 0);
                        }
                    } else {
                        // Send only to new client for existing rounds
                        send(new_socket, buffer, strlen(buffer), 0);
                    }
                }
            } else {
                close(new_socket);
            }
        }
        
        for (i = 0; i < numclient; i++) {
            if (FD_ISSET(clientsockfd[i], &tempfds)) {
                memset(buffer, 0, BUFFER_SIZE);
                valread = read(clientsockfd[i], buffer, BUFFER_SIZE);
                
                if (valread <= 0) {
                    getpeername(clientsockfd[i], (struct sockaddr*)&client_addr, &addr_size);
                    printf("Client %s:%d disconnected\n", 
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    
                    close(clientsockfd[i]);
                    FD_CLR(clientsockfd[i], &readfds);
                    
                    for (int j = i; j < numclient - 1; j++) {
                        clientsockfd[j] = clientsockfd[j+1];
                        client_sent[j] = client_sent[j+1];
                        client_numbers[j] = client_numbers[j+1];
                    }
                    numclient--;
                    i--;
                } else {
                    if (numclient < 2) {
                        getpeername(clientsockfd[i], (struct sockaddr*)&client_addr, &addr_size);
                        printf("Server: Insufficient clients, \"%s\" from client %s:%d dropped\n", 
                               buffer, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    } else {
                        if (client_sent[i]) {
                            sprintf(buffer, "Server: Duplicate messages for Round %d are not allowed. Please wait for the results for Round %d and Call for the number for Round %d.", 
                                    round, round, round + 1);
                            send(clientsockfd[i], buffer, strlen(buffer), 0);
                        } else {
                            int num = atoi(buffer);
                            client_numbers[i] = num;
                            client_sent[i] = 1;
                            
                            int all_sent = 1;
                            for (int j = 0; j < numclient; j++) {
                                if (!client_sent[j]) {
                                    all_sent = 0;
                                    break;
                                }
                            }
                            
                            if (all_sent) {
                                int max_num = client_numbers[0];
                                int max_client = 0;
                                for (int j = 1; j < numclient; j++) {
                                    if (client_numbers[j] > max_num) {
                                        max_num = client_numbers[j];
                                        max_client = j;
                                    }
                                }
                                
                                struct sockaddr_in winner_addr;
                                addr_size = sizeof(winner_addr);
                                getpeername(clientsockfd[max_client], (struct sockaddr*)&winner_addr, &addr_size);
                                
                                sprintf(buffer, "Server: Maximum Number Received in Round %d is: %d. The number has been received from the client %s:%d\nServer: Enter the number for Round %d:", 
                                        round, max_num, inet_ntoa(winner_addr.sin_addr), ntohs(winner_addr.sin_port), round + 1);
                                
                                for (int j = 0; j < numclient; j++) {
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
