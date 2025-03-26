/*
Name: Shivam Choudhury
Roll number: 22CS10072
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define CLDP_PROTOCOL 253 /* Custom protocol number */
#define BUFFER_SIZE 2048
#define MAX_PAYLOAD 1024
#define RESPONSE_TIMEOUT 5 /* Seconds to wait for responses */
#define DEST_IP "192.168.137.72"

/* CLDP Message Types */
#define CLDP_HELLO 0x01
#define CLDP_QUERY 0x02
#define CLDP_RESPONSE 0x03

/* Metadata Types */
#define METADATA_HOSTNAME 0x01
#define METADATA_SYSTIME 0x02
#define METADATA_CPULOAD 0x03

/* CLDP Header Structure */
struct cldp_header
{
    uint8_t msg_type;
    uint8_t payload_length;
    uint16_t transaction_id;
    uint32_t reserved;
};

/* CLDP Query Structure */
struct cldp_query
{
    uint8_t metadata_type;
};

/* Linked list to store transaction id and destination addresses */
struct node
{
    uint16_t tid;                 // transaction id
    struct sockaddr_in dest_addr; // destination address
    struct node *next;
};

/* Global variables */
int running;
int sock_fd;
struct node *head = NULL;

/* Signal handler for graceful shutdown */
void handle_signal(int sig);
/* Calculate IP header checksum */
unsigned short calculate_checksum(void *b, int len);
/* Send a query to all nodes */
void send_query(int sock, uint8_t metadata_type);

void print_usage(const char *program_name);

void process_response(char *buffer, int bytes_received);

void discover_servers();

int main(int argc, char *argv[])
{
    running = 1;
    srand(time(NULL));
    char buffer[BUFFER_SIZE];

    /* Check command line arguments */
    if (argc < 2)
    {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Set up signal handler */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Create raw socket */
    sock_fd = socket(AF_INET, SOCK_RAW, CLDP_PROTOCOL);

    /* Set IP_HDRINCL option */
    int opt = 1;
    setsockopt(sock_fd, IPPROTO_IP, IP_HDRINCL, &opt, sizeof(opt));

    printf("Discovering servers...\n");
    discover_servers();

    if (strcmp(argv[1], "query") == 0)
    {
        if (argc < 3)
        {
            printf("Error: Missing metadata type for query\n");
            print_usage(argv[0]);
            close(sock_fd);
            exit(EXIT_FAILURE);
        }

        if (strcmp(argv[2], "hostname") == 0)
        {
            send_query(sock_fd, METADATA_HOSTNAME);
        }
        else if (strcmp(argv[2], "systime") == 0)
        {
            send_query(sock_fd, METADATA_SYSTIME);
        }
        else if (strcmp(argv[2], "cpuload") == 0)
        {
            send_query(sock_fd, METADATA_CPULOAD);
        }
        else if (strcmp(argv[2], "all") == 0)
        {
            send_query(sock_fd, METADATA_HOSTNAME);
            send_query(sock_fd, METADATA_SYSTIME);
            send_query(sock_fd, METADATA_CPULOAD);
        }
        else
        {
            printf("Error: Unknown metadata type '%s'\n", argv[2]);
            print_usage(argv[0]);
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        printf("Error: Unknown command '%s'\n", argv[1]);
        print_usage(argv[0]);
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    while (running)
    {
        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);
        // Set non blocking recvfrom
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

        int bytes_received = recvfrom(sock_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&sender_addr, &addr_len);
        if (bytes_received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                printf("No more responses\n");
                continue;
            }
            else
            {
                perror("recvfrom() failed");
                continue;
            }
        }

        else if (bytes_received == 0)
        {
            printf("No response received\n");
            continue;
        }

        else
        {
            process_response(buffer, bytes_received);
        }
    }

    // Free the linked list
    struct node *travel = head;
    while (travel != NULL)
    {
        struct node *temp = travel;
        travel = travel->next;
        free(temp);
    }

    /* Clean up */
    close(sock_fd);
    return 0;
}

/* Signal handler for graceful shutdown */
void handle_signal(int sig)
{
    running = 0;
}

/* Calculate IP header checksum */
unsigned short calculate_checksum(void *b, int len)
{
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char *)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

/* Send a query to all nodes */
void send_query(int sock, uint8_t metadata_type)
{
    // send to all the nodes in the linked list
    struct node *travel = head;
    while (travel != NULL)
    {
        struct sockaddr_in dest_addr = travel->dest_addr;
        dest_addr.sin_family = AF_INET;

        char buffer[29];
        memset(buffer, 0, sizeof(buffer));

        struct iphdr *ip_header = (struct iphdr *)buffer;
        ip_header->version = 4;
        ip_header->ihl = 5;
        ip_header->tos = 0;
        ip_header->tot_len = htons(20 + 8 + 1);
        ip_header->id = htons(rand() % 65535);
        ip_header->frag_off = 0;
        ip_header->ttl = 64;
        ip_header->protocol = CLDP_PROTOCOL;
        ip_header->saddr = inet_addr(DEST_IP);
        ip_header->daddr = dest_addr.sin_addr.s_addr;
        ip_header->check = calculate_checksum((void *)ip_header, sizeof(struct iphdr));

        struct cldp_header *cldp_hdr = (struct cldp_header *)(buffer + 20);
        cldp_hdr->msg_type = CLDP_QUERY;
        cldp_hdr->payload_length = 1;
        if (travel->tid == 0)
        {
            cldp_hdr->transaction_id = htons(rand() % 65535);
            travel->tid = ntohs(cldp_hdr->transaction_id);
        }
        else
            cldp_hdr->transaction_id = htons(travel->tid);
        cldp_hdr->reserved = 0;

        buffer[20 + 8] = metadata_type;

        sendto(sock, buffer, 20 + 8 + 1, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        travel = travel->next;
    }
}

/* Print usage information */
void print_usage(const char *program_name)
{
    printf("Usage: %s <command>\n", program_name);
    printf("Commands:\n");
    printf("  discover              - Discover nodes on the network\n");
    printf("  query hostname        - Query all nodes for their hostnames\n");
    printf("  query systime         - Query all nodes for their system times\n");
    printf("  query cpuload         - Query all nodes for their CPU loads\n");
    printf("  query all             - Query all nodes for all metadata types\n");
}

void process_response(char *buffer, int bytes_received)
{
    // extract IP header
    struct iphdr *ip_hdr = (struct iphdr *)buffer;

    // check protocol and checksum
    if (ip_hdr->protocol != CLDP_PROTOCOL)
    {
        printf("Unknown protocol\n");
        return;
    }

    unsigned short received_checksum = ip_hdr->check;
    ip_hdr->check = 0;
    unsigned short computed_checksum = calculate_checksum((void *)ip_hdr, sizeof(struct iphdr));
    if (received_checksum != computed_checksum)
    {
        printf("Checksum failed\n");
        return;
    }
    // Extract the server address
    struct in_addr response_ip;
    response_ip.s_addr = ip_hdr->saddr;

    // extract CLDP header
    struct cldp_header *cldp_hdr = (struct cldp_header *)(buffer + 20);
    uint8_t msg_type = cldp_hdr->msg_type;
    uint8_t payload_length = cldp_hdr->payload_length;
    uint16_t tid = ntohs(cldp_hdr->transaction_id);

    // check if the transaction id and the destination address match in the linked list
    struct node *travel = head;
    int found = 0;

    while (travel != NULL)
    {
        if (travel->tid == tid && travel->dest_addr.sin_addr.s_addr == ip_hdr->saddr)
        {
            found = 1;
            break;
        }

        travel = travel->next;
    }
    if (!found)
    {
        printf("Transaction ID not found\n");
        return;
    }

    // extract metadata
    switch (msg_type)
    {
    case CLDP_RESPONSE:
    {
        char metadata_value[MAX_PAYLOAD];
        memcpy(metadata_value, buffer + 20 + 8, payload_length);
        metadata_value[payload_length] = '\0';

        printf("\nReceived response from %s\n", inet_ntoa(response_ip));
        printf("Metadata: %s\n", metadata_value);
        break;
    }
    default:
        printf("Unknown message type\n");
        break;
    }

}

void discover_servers()
{
    char buffer[BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    fd_set readfds;
    struct timeval timeout;

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_SET(sock_fd, &readfds);

    printf("Waiting for HELLO messages...\n");

    while (1)
    {
        fd_set temp_fds = readfds; // Select modifies fd_set, so use a copy
        int activity = select(sock_fd + 1, &temp_fds, NULL, NULL, &timeout);

        if (activity < 0)
        {
            perror("select() failed");
            return;
        }
        else if (activity == 0)
        {
            printf("Discovery complete, no more HELLO messages received.\n");
            break; // Timeout occurred, exit discovery loop
        }

        // Data is available on the socket, receive it
        int bytes_received = recvfrom(sock_fd, buffer, BUFFER_SIZE, 0,
                                      (struct sockaddr *)&sender_addr, &addr_len);
        if (bytes_received < 0)
        {
            perror("recvfrom() failed");
            continue;
        }

        // Parse IP and CLDP headers
        struct iphdr *ip_header = (struct iphdr *)buffer;
        struct cldp_header *cldp_hdr = (struct cldp_header *)(buffer + sizeof(struct iphdr));

        if (ip_header->protocol == CLDP_PROTOCOL && cldp_hdr->msg_type == CLDP_HELLO)
        {
            printf("Discovered server: %s\n", inet_ntoa(sender_addr.sin_addr));

            // Check if already present in the linked list
            struct node *travel = head;
            int found = 0;
            while (travel != NULL)
            {
                if (travel->dest_addr.sin_addr.s_addr == sender_addr.sin_addr.s_addr)
                {
                    found = 1;
                    break;
                }
                travel = travel->next;
            }

            // Add new server to linked list
            if (!found)
            {
                struct node *new_node = (struct node *)malloc(sizeof(struct node));
                if (!new_node)
                {
                    perror("malloc failed");
                    exit(EXIT_FAILURE);
                }
                new_node->tid = 0;
                new_node->dest_addr = sender_addr;
                new_node->next = head;
                head = new_node;
            }
        }
    }
}
