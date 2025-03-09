#include <stdio.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#define PORT 5000

int main()
{
    int sock_fd;
    struct sockaddr_in server_;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    inet_aton("127.0.0.1", &server_.sin_addr.s_addr);
    server_.sin_family = AF_INET;
    server_.sin_port = htons(PORT);

    connect(sock_fd, (struct sockaddr *)&server_, sizeof(server_));

    close(sock_fd);
}