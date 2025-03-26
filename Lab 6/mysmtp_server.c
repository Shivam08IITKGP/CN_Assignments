/*=====================================
Name: Shivam Choudhury
Roll number: 22CS10072
=====================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
#define MAX_EMAIL_SIZE 4096
#define MAX_LINE_SIZE 1024

// Response codes
#define OK_RESPONSE "200 OK\r\n"
#define INVALID_SYNTAX "400 ERR Invalid command syntax\r\n"
#define NOT_FOUND "401 NOT FOUND Requested email does not exist\r\n"
#define FORBIDDEN "403 FORBIDDEN Action not permitted\r\n"
#define SERVER_ERROR "500 SERVER ERROR\r\n"

// Structure to store client session information
typedef struct
{
    int socket;
    char client_id[100];
    char sender[100];
    char recipient[100];
    int is_data_mode;
    char data_buffer[MAX_EMAIL_SIZE];
    struct sockaddr_in addr;
} client_session;

// Function prototypes
void *handle_client(void *arg);
void process_command(client_session *session, char *command);
void handle_helo(client_session *session, char *args);
void handle_mail_from(client_session *session, char *args);
void handle_rcpt_to(client_session *session, char *args);
void handle_data_content(client_session *session, char *line);
void handle_data(client_session *session);
void handle_list(client_session *session, char *args);
void handle_get_mail(client_session *session, char *args);
void save_email(client_session *session);
int validate_email(char *email);

int main(int argc, char *argv[])
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    pthread_t threads[MAX_CLIENTS];
    int thread_count = 0;

    // Check command line arguments
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Initialize server address struct
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind socket to address
    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    // Listen for incoming connections
    listen(server_fd, 5);

    // Create a directory to store emails
    mkdir("mailbox", 0777);

    printf("Listening on port %d...\n", port);

    // Accept incoming connections and handle them
    while (1)
    {
        client_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0)
        {
            perror("Accept failed");
            continue;
        }

        printf("Client connected: %s\n", inet_ntoa(client_addr.sin_addr));

        // Initialize client session
        client_session *session = (client_session *)malloc(sizeof(client_session));
        memset(session, 0, sizeof(client_session));
        session->socket = client_fd;
        session->addr = client_addr;
        session->is_data_mode = 0;

        // Create thread to handle client
        if (pthread_create(&threads[thread_count], NULL, handle_client, (void *)session) != 0)
        {
            perror("Thread creation failed");
            close(client_fd);
            free(session);
            continue;
        }

        // Detach thread to clean up resources automatically
        pthread_detach(threads[thread_count]);
        thread_count = (thread_count + 1) % MAX_CLIENTS;
    }

    // Close server socket (never reached in this simple implementation)
    close(server_fd);
    return 0;
}

// Handle client connection
void *handle_client(void *arg)
{
    client_session *session = (client_session *)arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    while (1)
    {
        bytes_received = recv(session->socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0)
        {
            break;
        }
        buffer[bytes_received] = '\0';

        // Split received data by lines
        char *line = strtok(buffer, "\r\n");
        while (line != NULL)
        {
            if (session->is_data_mode)
            {
                handle_data_content(session, line);
            }
            else
            {
                process_command(session, line);
            }
            line = strtok(NULL, "\r\n");
        }
    }

    // Client disconnected
    printf("Client disconnected.\n");
    close(session->socket);
    free(session);
    return NULL;
}

// Process client commands
void process_command(client_session *session, char *command)
{
    printf("Received command: %s\n", command);

    if (strncmp(command, "HELO ", 5) == 0)
    {
        handle_helo(session, command + 5);
    }
    else if (strncmp(command, "MAIL FROM: ", 11) == 0)
    {
        handle_mail_from(session, command + 11);
    }
    else if (strncmp(command, "RCPT TO: ", 9) == 0)
    {
        handle_rcpt_to(session, command + 9);
    }
    else if (strcmp(command, "DATA") == 0)
    {
        handle_data(session);
    }
    else if (strncmp(command, "LIST ", 5) == 0)
    {
        handle_list(session, command + 5);
    }
    else if (strncmp(command, "GET_MAIL ", 9) == 0)
    {
        handle_get_mail(session, command + 9);
    }
    else if (strcmp(command, "QUIT") == 0)
    {
        send(session->socket, "200 Goodbye\r\n", 13, 0);
        // Client will disconnect after this
    }
    else
    {
        send(session->socket, INVALID_SYNTAX, strlen(INVALID_SYNTAX), 0);
    }
}

// Handle HELO command
void handle_helo(client_session *session, char *args)
{
    if (strlen(args) > 0)
    {
        strncpy(session->client_id, args, sizeof(session->client_id) - 1);
        printf("HELO received from %s\n", session->client_id);
        send(session->socket, OK_RESPONSE, strlen(OK_RESPONSE), 0);
    }
    else
    {
        send(session->socket, INVALID_SYNTAX, strlen(INVALID_SYNTAX), 0);
    }
}

// Validate email format (simple check)
int validate_email(char *email)
{
    // Simple check for @ symbol
    return (strchr(email, '@') != NULL);
}

// Handle MAIL FROM command
void handle_mail_from(client_session *session, char *args)
{
    if (validate_email(args))
    {
        strncpy(session->sender, args, sizeof(session->sender) - 1);
        printf("MAIL FROM: %s\n", session->sender);
        send(session->socket, OK_RESPONSE, strlen(OK_RESPONSE), 0);
    }
    else
    {
        send(session->socket, INVALID_SYNTAX, strlen(INVALID_SYNTAX), 0);
    }
}

// Handle Data command
void handle_data(client_session* session)
{
    if (strlen(session->sender) == 0 || strlen(session->recipient) == 0)
    {
        send(session->socket, FORBIDDEN, strlen(FORBIDDEN), 0);
        return;
    }

    session->is_data_mode = 1;
    session->data_buffer[0] = '\0';
    send(session->socket, "Enter your message (end with a single dot '.'):\r\n", 47, 0);
}

// Handle RCPT TO command
void handle_rcpt_to(client_session *session, char *args)
{
    if (validate_email(args))
    {
        strncpy(session->recipient, args, sizeof(session->recipient) - 1);
        printf("RCPT TO: %s\n", session->recipient);
        send(session->socket, OK_RESPONSE, strlen(OK_RESPONSE), 0);
    }
    else
    {
        send(session->socket, INVALID_SYNTAX, strlen(INVALID_SYNTAX), 0);
    }
}

// Handle email content in DATA mode
void handle_data_content(client_session *session, char *line)
{
    // Check for end of data (single dot)
    if (strcmp(line, ".") == 0)
    {
        session->is_data_mode = 0;
        save_email(session);
        printf("DATA received, message stored.\n");
        send(session->socket, "200 Message stored successfully\r\n", 33, 0);
        return;
    }

    // Append line to data buffer
    size_t current_len = strlen(session->data_buffer);
    size_t line_len = strlen(line);

    if (current_len + line_len + 2 < MAX_EMAIL_SIZE)
    {
        strcat(session->data_buffer, line);
        strcat(session->data_buffer, "\n");
    }
    else
    {
        // Data too large, exit data mode
        session->is_data_mode = 0;
        send(session->socket, SERVER_ERROR, strlen(SERVER_ERROR), 0);
    }
}

// Save email to recipient's mailbox
void save_email(client_session *session)
{
    char filename[256];
    snprintf(filename, sizeof(filename), "mailbox/%s.txt", session->recipient);

    FILE *file = fopen(filename, "a");
    if (!file)
    {
        perror("Failed to open mailbox file");
        return;
    }

    // Get current date
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char date[20];
    strftime(date, sizeof(date), "%d-%m-%Y", t);

    // Write email header and content to file
    fprintf(file, "From: %s\n", session->sender);
    fprintf(file, "Date: %s\n", date);
    fprintf(file, "%s", session->data_buffer);
    fprintf(file, "---\n"); // Simple separator between emails

    fclose(file);
}

// Handle LIST command
void handle_list(client_session *session, char *args)
{
    printf("LIST %s\n", args);

    if (!validate_email(args))
    {
        send(session->socket, INVALID_SYNTAX, strlen(INVALID_SYNTAX), 0);
        return;
    }

    char filename[256];
    snprintf(filename, sizeof(filename), "mailbox/%s.txt", args);

    FILE *file = fopen(filename, "r");
    if (!file)
    {
        send(session->socket, NOT_FOUND, strlen(NOT_FOUND), 0);
        return;
    }

    // Send OK response
    send(session->socket, OK_RESPONSE, strlen(OK_RESPONSE), 0);

    char line[MAX_LINE_SIZE];
    int email_id = 0;
    char sender[100];
    char date[20];
    char response[BUFFER_SIZE];
    int reading_header = 0;

    while (fgets(line, sizeof(line), file))
    {
        line[strcspn(line, "\n")] = 0; // Remove newline

        if (strncmp(line, "From: ", 6) == 0 && !reading_header)
        {
            email_id++;
            reading_header = 1;
            strcpy(sender, line + 6);

            // Get date
            if (fgets(line, sizeof(line), file))
            {
                line[strcspn(line, "\n")] = 0;
                sscanf(line, "Date: %s", date);
            }

            // Create and send response line
            snprintf(response, sizeof(response), "%d: Email from %s (%s)\r\n",
                     email_id, sender, date);
            send(session->socket, response, strlen(response), 0);
        }

        // Check for email separator
        if (strcmp(line, "---") == 0)
        {
            reading_header = 0;
        }
    }

    fclose(file);
    printf("Emails retrieved; list sent.\n");
}

// Handle GET_MAIL command
void handle_get_mail(client_session *session, char *args)
{
    char email[100];
    int id;
    if (sscanf(args, "%s %d", email, &id) != 2 || !validate_email(email) || id <= 0)
    {
        send(session->socket, INVALID_SYNTAX, strlen(INVALID_SYNTAX), 0);
        return;
    }

    printf("GET_MAIL %s %d\n", email, id);

    char filename[256];
    snprintf(filename, sizeof(filename), "mailbox/%s.txt", email);

    FILE *file = fopen(filename, "r");
    if (!file)
    {
        send(session->socket, NOT_FOUND, strlen(NOT_FOUND), 0);
        return;
    }

    char line[MAX_LINE_SIZE];
    int email_id = 0;
    int found = 0;
    int reading_email = 0;
    char response[MAX_EMAIL_SIZE];
    memset(response, 0, MAX_EMAIL_SIZE);

    while (fgets(line, sizeof(line), file))
    {
        line[strcspn(line, "\n")] = 0; // Remove newline

        // Check for start of email (From: line)
        if (strncmp(line, "From: ", 6) == 0 && !reading_email)
        {
            email_id++;

            if (email_id == id)
            {
                found = 1;
                reading_email = 1;
                strcat(response, OK_RESPONSE);
                strcat(response, line);
                strcat(response, "\r\n");
            }
        }
        // Add content to response if this is the requested email
        else if (reading_email)
        {
            // Check for email separator
            if (strcmp(line, "---") == 0)
            {
                reading_email = 0;
                break;
            }

            strcat(response, line);
            strcat(response, "\r\n");
        }
    }

    fclose(file);

    if (found)
    {
        send(session->socket, response, strlen(response), 0);
        printf("Email with id %d sent.\n", id);
    }
    else
    {
        send(session->socket, NOT_FOUND, strlen(NOT_FOUND), 0);
    }
}