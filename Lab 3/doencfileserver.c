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
#include <netinet/in.h>

#define PORT 5000
#define BUFFER_SIZE 100

// Handles receiving data from the client, encrypting it, and sending it back.
void recieve_encrypt_send(int client_sock, char *client_ip, int client_port, char *key)
{
    char filename[256], enc_filename[512], buffer[BUFFER_SIZE + 1];

    // Generate filenames based on client IP and port
    snprintf(filename, sizeof(filename), "%s.%d.txt", client_ip, client_port);
    snprintf(enc_filename, sizeof(enc_filename), "%s.enc", filename);

    // Open file to store received data
    FILE *fp = fopen(filename, "w");
    if (!fp)
    {
        perror("Error creating temp file");
        return;
    }

    // Receive file data from client
    int bytes_received;
    while ((bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0)
    {
        buffer[bytes_received] = '\0'; // Null-terminate received data

        // Stop receiving if "EOF_MARKER" is found
        char *eof_pos = strstr(buffer, "EOF_MARKER");
        if (eof_pos)
        {
            *eof_pos = '\0'; // Trim data at EOF_MARKER
            fwrite(buffer, 1, strlen(buffer), fp);
            break;
        }

        fwrite(buffer, 1, bytes_received, fp);
    }
    fclose(fp);

    // Open received file for reading and create encrypted output file
    fp = fopen(filename, "r");
    FILE *enc_fp = fopen(enc_filename, "w");
    if (!enc_fp)
    {
        perror("Error opening files for encryption");
        return;
    }

    // Encrypt file content using the key and write to encrypted file
    while (fgets(buffer, BUFFER_SIZE, fp) != NULL)
    {
        for (int i = 0; buffer[i] != '\0'; i++)
        {
            if (buffer[i] >= 'A' && buffer[i] <= 'Z')
                buffer[i] = key[buffer[i] - 'A'];
            else if (buffer[i] >= 'a' && buffer[i] <= 'z')
                buffer[i] = key[buffer[i] - 'a'] + 'a' - 'A';
        }
        fputs(buffer, enc_fp);
    }

    fclose(fp);
    fclose(enc_fp);

    // Send the encrypted file back to the client
    enc_fp = fopen(enc_filename, "r");
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, enc_fp)) > 0)
    {
        send(client_sock, buffer, bytes_read, 0);
    }
    fclose(enc_fp);

    // Clean up temporary files
    remove(filename);
    remove(enc_filename);
}

int main()
{
    int sock_fd, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int addr_size;
    char client_ip[INET_ADDRSTRLEN], key[27];

    // Create a TCP socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    // Configure server address and port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind the socket to the specified address and port
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        return 1;
    }

    // Start listening for client connections
    listen(sock_fd, 5);

    while (1)
    {
        // Accept an incoming client connection
        addr_size = sizeof(client_addr);
        client_sock = accept(sock_fd, (struct sockaddr *)&client_addr, &addr_size);
        if (client_sock < 0)
        {
            perror("Accept error");
            continue;
        }

        // Get client IP address and port
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);
        printf("Connected to %s:%d\n", client_ip, client_port);

        // Receive encryption key from client
        recv(client_sock, key, 26, 0);
        key[26] = '\0'; // Ensure null termination

        // Process file transfer and encryption
        recieve_encrypt_send(client_sock, client_ip, client_port, key);

        // Close client connection
        close(client_sock);

        printf("Disconnected from %s:%d\n", client_ip, client_port);
    }

    // Close server socket
    close(sock_fd);
    return 0;
}
