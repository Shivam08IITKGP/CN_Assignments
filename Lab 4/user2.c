#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ksocket.h"z

#define MESSAGE_SIZE 512

int main()
{
    int sockfd;
    FILE *file;
    char buffer[MESSAGE_SIZE];
    ssize_t bytes_received;

    // Create KTP socket
    sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0)
    {
        perror("Failed to create KTP socket");
        exit(EXIT_FAILURE);
    }

    // Bind source and destination addresses
    if (k_bind(sockfd, "127.0.0.1", 6000, "127.0.0.1", 5000) < 0)
    {
        perror("Failed to bind KTP socket");
        exit(EXIT_FAILURE);
    }

    // Open the output file
    file = fopen("output.txt", "wb");
    if (!file)
    {
        perror("Failed to open output.txt");
        exit(EXIT_FAILURE);
    }

    // Receive the file in chunks of MESSAGE_SIZE
    while ((bytes_received = k_recvfrom(sockfd, buffer, MESSAGE_SIZE)) > 0)
    {
        if (strcmp(buffer, "EOF") == 0)
        {
            break;
        }
        fwrite(buffer, 1, bytes_received, file);
        printf("Received %zd bytes.\n", bytes_received);
    }

    printf("File reception completed.\n");

    // Close the file
    fclose(file);

    // Close the socket
    if (k_close(sockfd) < 0)
    {
        perror("Failed to close KTP socket");
        exit(EXIT_FAILURE);
    }

    return 0;
}
