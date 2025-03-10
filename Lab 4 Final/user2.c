#include "ksocket.h"

#define MESSAGE_SIZE 512

int main()
{
    initialize_shared_memory();
    int sockfd;
    FILE *file;
    char buffer[MESSAGE_SIZE - 2];
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

    file = fopen("output.txt", "w");
    if (!file)
    {
        perror("Failed to open output.txt");
        exit(EXIT_FAILURE);
    }

    fclose(file);
    file = NULL;
    // Receive the file in chunks of MESSAGE_SIZE
    while (1)
    {
        bytes_received = k_recvfrom(sockfd, buffer, MESSAGE_SIZE - 2);
        if (bytes_received <= 0)
        {
            // printf("Not received any data.\n");
            sleep(1);
            continue;
        }

        if (strcmp(buffer, "EOF") == 0)
        {
            break;
        }
        if (bytes_received > 0)
        {
            file = fopen("output.txt", "a");
            if (!file)
            {
                perror("Failed to open output.txt");
                exit(EXIT_FAILURE);
            }
            fwrite(buffer, 1, bytes_received, file);
            printf("Received %zd bytes.\n", bytes_received);
            fclose(file);
        }
    }

    printf("File reception completed.\n");

    // Close the file
    fclose(file);
    sleep((unsigned int)(10000 * P));

    // Close the socket
    if (k_close(sockfd) < 0)
    {
        perror("Failed to close KTP socket");
        exit(EXIT_FAILURE);
    }

    return 0;
}
