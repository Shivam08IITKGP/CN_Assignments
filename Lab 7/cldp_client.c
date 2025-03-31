/*
Name: Shivam Choudhury
Roll number: 22CS10072
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>
#include <ifaddrs.h>
#include <errno.h>
#include <time.h>

#define CLDP_PROTOCOL 253 /* Custom protocol number */
#define BUFFER_SIZE 2048
#define MAX_PAYLOAD 1024
#define RESPONSE_TIMEOUT 5 /* Seconds to wait for responses */

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

in_addr_t get_local_ip();

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
            else if (running == 0)
            {
                printf("Sig child called\n");
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
    printf("Freed all nodes\n");

    /* Clean up */
    close(sock_fd);
    return 0;
}

/* Signal handler for graceful shutdown */
void handle_signal(int sig)
{
    running = 0;
    printf("\nShutting down client...\n");
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
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = travel->dest_addr.sin_addr.s_addr;
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
        ip_header->daddr = dest_addr.sin_addr.s_addr;
        ip_header->saddr = get_local_ip();
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

/* Process the response from the server */
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
        if (travel->dest_addr.sin_addr.s_addr == ip_hdr->saddr)
        {
            found = 1;
            break;
        }

        travel = travel->next;
    }
    if (!found || travel->tid != tid)
    {
        printf("Server address not found: %s\n", inet_ntoa(response_ip));
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
    }
}

void discover_servers()
{
    char buffer[BUFFER_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    printf("Waiting for HELLO messages...\n");

    struct timeval start_time, current_time;
    gettimeofday(&start_time, NULL);
    const int discovery_timeout = 10;

    while (1)
    {
        fd_set readfds;
        struct timeval timeout, elapsed;

        // Calculate remaining timeout
        gettimeofday(&current_time, NULL);
        timersub(&current_time, &start_time, &elapsed);
        if (elapsed.tv_sec >= discovery_timeout)
        {
            printf("Discovery timeout reached\n");
            break;
        }

        // Set fresh timeout for each select call
        timeout.tv_sec = discovery_timeout - elapsed.tv_sec;
        timeout.tv_usec = 0;

        FD_ZERO(&readfds);
        FD_SET(sock_fd, &readfds);

        int activity = select(sock_fd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0)
        {
            perror("select() failed");
            return;
        }
        if (activity == 0)
        {
            printf("Discovery complete\n");
            break;
        }

        // Receive packet with proper error handling
        ssize_t bytes_received = recvfrom(sock_fd, buffer, BUFFER_SIZE, 0,
                                          (struct sockaddr *)&sender_addr, &addr_len);
        if (bytes_received < 0)
        {
            perror("recvfrom() failed");
            continue;
        }

        // Verify minimum packet size
        if (bytes_received < (ssize_t)(sizeof(struct iphdr) + sizeof(struct cldp_header)))
        {
            fprintf(stderr, "Received undersized packet (%zd bytes)\n", bytes_received);
            continue;
        }

        // Parse headers using proper alignment
        struct iphdr *ip_header = (struct iphdr *)buffer;
        struct cldp_header *cldp_hdr = (struct cldp_header *)(buffer + sizeof(struct iphdr));

        // Validate protocol and message type
        if (ip_header->protocol != CLDP_PROTOCOL || cldp_hdr->msg_type != CLDP_HELLO)
        {
            continue;
        }

        // Get actual source IP from IP header (critical fix)
        struct in_addr actual_src;
        actual_src.s_addr = ip_header->saddr;

        printf("Discovered server: %s\n", inet_ntoa(actual_src)); // Fixed logging

        // Check for existing entries using correct IP comparison
        struct node *current = head;
        int exists = 0;
        while (current)
        {
            if (current->dest_addr.sin_addr.s_addr == actual_src.s_addr)
            {
                exists = 1;
                break;
            }
            current = current->next;
        }

        if (!exists)
        {
            // Create properly initialized node
            struct node *new_node = (struct node *)malloc(sizeof(struct node));
            if (!new_node)
            {
                perror("malloc failed");
                exit(EXIT_FAILURE);
            }

            // Initialize all fields
            memset(new_node, 0, sizeof(*new_node));
            new_node->dest_addr.sin_family = AF_INET;
            new_node->dest_addr.sin_addr.s_addr = actual_src.s_addr;
            new_node->next = head;
            head = new_node;

            printf("New server registered: %s:%d\n",
                   inet_ntoa(new_node->dest_addr.sin_addr),
                   ntohs(new_node->dest_addr.sin_port));
        }
    }
}

in_addr_t get_local_ip()
{
    struct ifaddrs *ifaddr, *ifa;
    in_addr_t ip = htonl(INADDR_LOOPBACK);

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs failed");
        return ip;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET &&
            strcmp(ifa->ifa_name, "lo") != 0)
        {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            ip = addr->sin_addr.s_addr;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return ip;
}