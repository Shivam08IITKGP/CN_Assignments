/*
Shivam Choudhury
22CS10072
https://drive.google.com/file/d/1tAhyzFNWA8D0XCEoy2v5lI3VhZ3zqDFg/view?usp=sharing
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 5000
#define BUFFER_SIZE 100

// Sends a file's contents to the server.
void send_file(int sockfd, FILE *fp)
{
    char buffer[BUFFER_SIZE]; // Buffer for file chunks
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
    {
        send(sockfd, buffer, bytes_read, 0); // Send exactly the number of bytes read
    }

    // Mark the end of file transmission
    send(sockfd, "EOF_MARKER", strlen("EOF_MARKER"), 0);
}

// Receives the encrypted file from the server and saves it locally.
void receive_file(int sockfd, const char *filename)
{
    char buffer[BUFFER_SIZE];
    char enc_filename[256];
    snprintf(enc_filename, sizeof(enc_filename), "%s.enc", filename);

    FILE *fp = fopen(enc_filename, "w");
    if (!fp)
    {
        perror("Failed to create encrypted file");
        return;
    }

    int bytes_received;
    while ((bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0)) > 0)
    {
        fwrite(buffer, 1, bytes_received, fp);
    }
    fclose(fp);
    printf("File encrypted: %s -> %s\n", filename, enc_filename);
}

int main()
{
    int sockfd;
    struct sockaddr_in server_addr;
    char filename[256], key[27];

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Unable to create socket");
        exit(0);
    }

    // Configure server address and port
    server_addr.sin_family = AF_INET;
    inet_aton("127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(PORT);

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Unable to connect to server");
        exit(0);
    }

    while (1)
    {
        // Get the filename from user input
        printf("Enter filename: ");
        scanf("%s", filename);

        // Try opening the file
        FILE *fp = fopen(filename, "r");
        if (!fp)
        {
            printf("NOTFOUND %s\n", filename);
            continue;
        }

        // Get encryption key from user
        printf("Enter key (26 letters): ");
        scanf("%s", key);
        if (strlen(key) != 26)
        {
            printf("Invalid key length. Try again.\n");
            fclose(fp);
            continue;
        }

        // Send encryption key to the server
        send(sockfd, key, 26, 0);

        // Send file to the server
        send_file(sockfd, fp);
        fclose(fp);

        // Receive encrypted file from server
        receive_file(sockfd, filename);

        // Ask user if they want to encrypt another file
        printf("Encrypt another file? (Yes/No): ");
        char response[10];
        scanf("%s", response);
        if (strcmp(response, "No") == 0)
        {
            break;
        }
    }

    // Close the socket connection
    close(sockfd);
    return 0;
}
