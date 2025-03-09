#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ksocket.h"

#define MESSAGE_SIZE 512

int main()
{
    int sockfd;
    FILE *file;
    char message[MESSAGE_SIZE];
    size_t bytes_read;

    // Create KTP socket
    sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0)
    {
        perror("Failed to create KTP socket");
        exit(EXIT_FAILURE);
    }

    // Bind source and destination addresses
    if (k_bind(sockfd, "127.0.0.1", 5000, "127.0.0.1", 6000) < 0)
    {
        perror("Failed to bind KTP socket");
        exit(EXIT_FAILURE);
    }

    // Open the input file
    file = fopen("input.txt", "rb");
    if (!file)
    {
        perror("Failed to open input.txt");
        exit(EXIT_FAILURE);
    }

    // Read and send the file in chunks of MESSAGE_SIZE
    while ((bytes_read = fread(message, 1, MESSAGE_SIZE, file)) > 0)
    {
        if (k_sendto(sockfd, message, bytes_read) < 0)
        {
            perror("Failed to send message");
            fclose(file);
            exit(EXIT_FAILURE);
        }
        printf("Sent %zu bytes.\n", bytes_read);
    }
    
    char *end_marker = "EOF";
    k_sendto(sockfd, end_marker, sizeof(end_marker));
    
    printf("File transmission completed.\n");

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
