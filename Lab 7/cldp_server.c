/*
Name: Shivam Choudhury
Roll number: 22CS10072
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <ifaddrs.h>

#define BUFFER_SIZE 2048
#define HOSTNAME_LEN 256
#define SYSTIME_LEN 20
#define CPULOAD_LEN 33
#define CLDP_PROTOCOL 253
#define HELLO_TYPE 0x01
#define QUERY_TYPE 0x02
#define RESPONSE_TYPE 0x03

// Metadata types
#define METADATA_HOSTNAME 0x01
#define METADATA_SYSTIME 0x02
#define METADATA_CPULOAD 0x03

int running;

struct cldp_header
{
    uint8_t msg_type;        // What kind of message is it
    uint8_t payload_length;  // how much extra data is there appart from this header
    uint16_t transaction_id; // a unique number to track requests and responses,
    uint32_t reserved;       // reserved for future use (set 0 for now)
};

void handle_signal(int sig);
void get_hostname(char *buffer, int max_len);
void get_system_time(char *buffer, int max_len);
void get_cpu_load(char *buffer, int max_len);
unsigned short calculate_checksum(void *b, int len);
void send_hello(int sockfd);
in_addr_t get_local_ip();

int main()
{
    srand(time(NULL));
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    unsigned char buffer[BUFFER_SIZE];

    // Set up signal handler
    signal(SIGINT, handle_signal);

    sockfd = socket(AF_INET, SOCK_RAW, CLDP_PROTOCOL);
    int broadcast = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    // Set IP_HDRINCL option, to create IP Header manually
    int opt = 1;
    setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &opt, sizeof(opt));

    // Bind to specific interface
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind to port
    bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    running = 1;

    time_t start = time(NULL);
    send_hello(sockfd);

    while (running)
    {
        if (time(NULL) - start > 10)
        {
            // Send HELLO message
            send_hello(sockfd);
            start = time(NULL);
        }

        // Set non blocking recvfrom call for 1 second time out
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

        int bytes_received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
        if (bytes_received <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
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
                break;
            }
        }

        // Extract the IP header
        struct iphdr *ip_hdr = (struct iphdr *)buffer;

        // Check the protocol first
        if (ip_hdr->protocol != CLDP_PROTOCOL)
        {
            printf("DEBUG: Unknown protocol came in\n");
            continue;
        }

        // Check the checksum
        unsigned short received_checksum = ip_hdr->check;
        ip_hdr->check = 0;
        unsigned short computed_checksum = calculate_checksum((void *)ip_hdr, sizeof(struct iphdr));

        if (received_checksum != computed_checksum)
        {
            printf("DEBUG: Checksum failed\n");
            continue;
        }

        // Extract the CLDP header
        struct cldp_header *cldp_hdr = (struct cldp_header *)(buffer + sizeof(struct iphdr));

        char *metadata_value = NULL;

        int further = 0;
        switch (cldp_hdr->msg_type)
        {
        case HELLO_TYPE:
        {
            break;
        }
        case QUERY_TYPE:
        {
            further = 1;
            printf("\nReceived packet from %s\n", inet_ntoa(client_addr.sin_addr));
            printf("Received QUERY message\n");
            uint8_t metadata_type = buffer[sizeof(struct iphdr) + sizeof(struct cldp_header)];
            switch (metadata_type)
            {
            case METADATA_HOSTNAME:
            {
                printf("Received HOSTNAME query\n");
                metadata_value = (char *)malloc(HOSTNAME_LEN);
                get_hostname(metadata_value, HOSTNAME_LEN);
                break;
            }
            case METADATA_SYSTIME:
            {
                printf("Received SYSTIME query\n");
                metadata_value = (char *)malloc(SYSTIME_LEN);
                get_system_time(metadata_value, SYSTIME_LEN);
                break;
            }
            case METADATA_CPULOAD:
            {
                printf("Received CPULOAD query\n");
                metadata_value = (char *)malloc(CPULOAD_LEN);
                get_cpu_load(metadata_value, CPULOAD_LEN);
                break;
            }
            default:
            {
                metadata_value = strdup("Invalid query");
                break;
            }
            }
            break;
        }
        case RESPONSE_TYPE:
        {
            break;
        }
        }
        if (!further)
            continue;

        ip_hdr->daddr = ip_hdr->saddr;
        ip_hdr->saddr = get_local_ip();
        ip_hdr->tot_len = htons(sizeof(struct iphdr) + sizeof(struct cldp_header) + strlen(metadata_value));
        ip_hdr->id = htons(rand() % 65535);
        ip_hdr->check = calculate_checksum((void *)ip_hdr, sizeof(struct iphdr));

        // Set up CLDP Header
        cldp_hdr->msg_type = RESPONSE_TYPE;
        cldp_hdr->payload_length = strlen(metadata_value);

        memcpy(buffer, ip_hdr, sizeof(struct iphdr));
        memcpy(buffer + sizeof(struct iphdr), cldp_hdr, sizeof(struct cldp_header));
        memcpy(buffer + sizeof(struct iphdr) + sizeof(struct cldp_header), metadata_value, strlen(metadata_value));

        client_addr.sin_family = AF_INET;
        client_addr.sin_addr.s_addr = ip_hdr->daddr;

        // Send the packet
        sendto(sockfd, buffer, ntohs(ip_hdr->tot_len), 0, (struct sockaddr *)&client_addr, client_len);
        start = time(NULL);
        printf("Sent RESPONSE message\n\n");
        free(metadata_value);
    }

    close(sockfd);
    return 0;
}

// Signal handler
void handle_signal(int sig)
{
    running = 0;
    printf("\nShutting down server...\n");
}

// Function to get system hostname
void get_hostname(char *buffer, int max_len)
{
    gethostname(buffer, max_len);
    buffer[max_len - 1] = '\0';
    // needs 256 bytes
}

// Function to get system time
void get_system_time(char *buffer, int max_len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    time_t nowtime = tv.tv_sec;
    struct tm *nowtm = localtime(&nowtime);

    strftime(buffer, max_len, "%Y-%m-%d %H:%M:%S", nowtm);
    // needs 20 bytes
}

// Function to get CPU load
void get_cpu_load(char *buffer, int max_len)
{
    struct sysinfo si;

    if (sysinfo(&si) == 0)
    {
        snprintf(buffer, max_len, "Load Avg: %.2f, %.2f, %.2f",
                 si.loads[0] / 65536.0, si.loads[1] / 65536.0, si.loads[2] / 65536.0);
    }
    else
    {
        snprintf(buffer, max_len, "CPU Load: Error");
    }
    // needs 33 bytes
}

void send_hello(int sockfd)
{
    char buffer[28];

    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    memset(buffer, 0, 28);
    struct iphdr *ip_header = (struct iphdr *)buffer;
    struct cldp_header *cldp_hdr = (struct cldp_header *)(buffer + sizeof(struct iphdr));

    // Fill IP Header (Only if using raw sockets)
    ip_header->version = 4;
    ip_header->ihl = 5;
    ip_header->tos = 0;
    ip_header->tot_len = htons(sizeof(buffer));
    ip_header->id = htons(rand() % 65535);
    ip_header->frag_off = 0;
    ip_header->ttl = 64;
    ip_header->protocol = CLDP_PROTOCOL;
    ip_header->saddr = get_local_ip();
    ip_header->daddr = inet_addr("255.255.255.255");
    ip_header->check = calculate_checksum((void *)ip_header, sizeof(struct iphdr));

    // Fill CLDP Header
    cldp_hdr->msg_type = HELLO_TYPE;
    cldp_hdr->payload_length = 0;
    cldp_hdr->transaction_id = htons(rand() % 65535);
    cldp_hdr->reserved = 0;

    // Send the packet
    if (sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0)
    {
        perror("sendto failed");
    }
    else
    {
        printf("Sent HELLO message\n");
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