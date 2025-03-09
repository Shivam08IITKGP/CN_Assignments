int opt = 1;
setsockopt(sockfd, SQL_SOCKET, SO_REUSADDR, &opt, sizeof(opt));