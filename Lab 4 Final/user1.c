#include "ksocket.h"

#define MESSAGE_SIZE 512

int main()
{
    initialize_shared_memory();

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
    while ((bytes_read = fread(message, 1, MESSAGE_SIZE - 2, file)) > 0)
    {
        while (k_sendto(sockfd, message, bytes_read) < 0)
        {
            sleep(2);
        }
        printf("Sent %zu bytes.\n", bytes_read);
    }

    // Send end-of-file marker
    const char *end_marker = "EOF";
    if (k_sendto(sockfd, end_marker, strlen(end_marker) + 1) < 0)
    {
        perror("Failed to send EOF marker");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    printf("File transmission completed.\n");

    // Close the file
    fclose(file);
    sleep((unsigned int)(10000 * P));

    // Close the socket
    if (k_close(sockfd) < 0)
    {
        perror("Failed to close KTP socket");
        exit(EXIT_FAILURE);
    }
    printf("Socket closed.\n");

    return 0;
}
