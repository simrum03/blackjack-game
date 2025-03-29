#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/select.h>

#define IPV4_MULTICAST_IP "239.255.255.250"
#define IPV6_MULTICAST_IP "ff02::1"
#define MULTICAST_PORT 12951
#define MAXLINE 1024

typedef enum {
    IPV4,
    IPV6
} IP_VERSION;

void receive_multicast(char *server_ip, IP_VERSION version) {
    int sockfd;
    char buffer[MAXLINE];

    if (version == IPV4) {
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);

        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(MULTICAST_PORT);

        if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("bind failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(IPV4_MULTICAST_IP);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);

        if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            perror("setsockopt failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        printf("Waiting for IPv4 server IP from multicast...\n");

        if (recvfrom(sockfd, buffer, MAXLINE, 0, (struct sockaddr *)&addr, &addrlen) < 0) {
            perror("recvfrom failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    } else {  // IPv6
        struct sockaddr_in6 addr;
        socklen_t addrlen = sizeof(addr);

        if ((sockfd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = htons(MULTICAST_PORT);

        if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("bind failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        struct ipv6_mreq mreq6;
        inet_pton(AF_INET6, IPV6_MULTICAST_IP, &mreq6.ipv6mr_multiaddr);
        mreq6.ipv6mr_interface = 0;  // Use default interface

        if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, sizeof(mreq6)) < 0) {
            perror("setsockopt failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        printf("Waiting for IPv6 server IP from multicast...\n");

        if (recvfrom(sockfd, buffer, MAXLINE, 0, (struct sockaddr *)&addr, &addrlen) < 0) {
            perror("recvfrom failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    strncpy(server_ip, buffer, MAXLINE);
    printf("Received server IP: %s\n", server_ip);

    close(sockfd);
}

void play_game(int sockfd) {
    char buffer[MAXLINE];
    int n;
    fd_set readfds;
    struct timeval tv;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        tv.tv_sec = 30;
        tv.tv_usec = 0;

        int activity = select(sockfd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0) {
            perror("select failed");
            break;
        } else if (activity == 0) {
            printf("Timeout, no data received\n");
            continue;
        }

        if (FD_ISSET(sockfd, &readfds)) {
            memset(buffer, 0, MAXLINE);
            n = recv(sockfd, buffer, MAXLINE, 0);

            if (n <= 0) {
                if (n == 0) {
                    printf("Server closed the connection\n");
                } else {
                    perror("recv failed");
                }
                break;
            }

            printf("Received from server: %s", buffer);


            if (strstr(buffer, "Draw a card? (yes/no):")) {
                memset(buffer, 0, MAXLINE);
                fgets(buffer, MAXLINE, stdin);
                buffer[strcspn(buffer, "\n")] = 0;

                if (strcmp (buffer, "yes") != 0 && strcmp(buffer, "no") != 0) {
                    printf("Invalid input. Please type 'yes' or 'no'.\n");
                    continue;
                }

                if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
                    perror("send failed");
                    break;
                }
            } else if (strstr(buffer, "Do you want it to be 1 or 11? (1/11):")) {
                memset(buffer, 0, MAXLINE);
                fgets(buffer, MAXLINE, stdin);
                buffer[strcspn(buffer, "\n")] = 0;
                if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
                    perror("send failed");
                    break;
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            memset(buffer, 0, MAXLINE);
            fgets(buffer, MAXLINE, stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
                perror("send failed");
                break;
            }
        }
    }
}

int main() {
    char server_ip[INET6_ADDRSTRLEN];
    int sockfd;
    IP_VERSION version;
    int choice = 0;

    printf("Choose IP version:\n");
    printf("1. IPv4\n");
    printf("2. IPv6\n");
    printf("Choice: ");
    
    if (scanf("%d", &choice) != 1) {
        fprintf(stderr, "Invalid input. Defaulting to IPv4.\n");
        choice = 1;
    }
    getchar();
    
    version = (choice == 2) ? IPV6 : IPV4;
    printf("Version selected: %s\n", (version == IPV6) ? "IPv6" : "IPv4");
    
    // Receive multicast with selected version
    receive_multicast(server_ip, version);
    
    if (version == IPV6) {  // IPV6
        struct sockaddr_in6 servaddr6;
        
        if ((sockfd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        memset(&servaddr6, 0, sizeof(servaddr6));
        servaddr6.sin6_family = AF_INET6;
        servaddr6.sin6_port = htons(MULTICAST_PORT);

        printf("Trying to connect using IPv6 address: %s\n", server_ip);

        if (inet_pton(AF_INET6, server_ip, &servaddr6.sin6_addr) <= 0) {
            perror("inet_pton failed for IPv6");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        if (connect(sockfd, (struct sockaddr *)&servaddr6, sizeof(servaddr6)) < 0) {
            perror("connect failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        
        printf("Successfully connected using IPv6\n");
    } 
    else {  // IPV4
        struct sockaddr_in servaddr;
        
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(MULTICAST_PORT);

        printf("Trying to connect using IPv4 address: %s\n", server_ip);

        if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) <= 0) {
            perror("inet_pton failed for IPv4");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
            perror("connect failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        
        printf("Successfully connected using IPv4\n");
    }

    printf("Connected to server at %s\n", server_ip);

    char buffer[MAXLINE];
    memset(buffer, 0, MAXLINE);
    if (recv(sockfd, buffer, MAXLINE, 0) <= 0) {
        perror("recv failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("%s", buffer);

    memset(buffer, 0, MAXLINE);
    fgets(buffer, MAXLINE, stdin);
    buffer[strcspn(buffer, "\n")] = 0;
    if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
        perror("send failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    play_game(sockfd);

    close(sockfd);
    return 0;
}