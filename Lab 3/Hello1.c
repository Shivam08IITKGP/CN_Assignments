#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define PORT 5000

int main()
{
    int sock_fd, client_fd;
    struct sockaddr_in server_, client_;
    char buffer[1024];

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_.sin_addr.s_addr = INADDR_ANY;
    server_.sin_port = htons(PORT);
    server_.sin_family = AF_INET;

    bind(sock_fd, (struct sockaddr *)&server_, sizeof(server_));

    listen(sock_fd, 5);

    while (1)
    {
        client_fd = accept(client_fd, (struct sockaddr *)&client_, sizeof(client_));
        memset(buffer, 0, sizeof(buffer));

        recv(sock_fd, buffer, sizeof(buffer), 0);
        int cnt = strlen(buffer);
        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, "%d", cnt);
        send(client_fd, buffer, sizeof(buffer), 0);

        close(client_fd);
    }
    close(sock_fd);
}