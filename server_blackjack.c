#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <net/if.h>

#define PORT 12951
#define MAX_PLAYERS 10
#define MAXLINE 1024
#define MAXFD 64
#define IPV4_MULTICAST_IP "239.255.255.250"
#define IPV6_MULTICAST_IP "ff02::1"
#define MULTICAST_PORT 12951

typedef struct {
    char name[50];
    int wins, draws, losses;
} Player;

Player players[MAX_PLAYERS];
int player_count = 0;

typedef enum {
    IPV4,
    IPV6
} IP_VERSION;

typedef struct {
    IP_VERSION version;
    char ip_address[INET6_ADDRSTRLEN];
} ServerConfig;

void get_local_ip(char *ip_buffer, size_t buffer_size, IP_VERSION version) {
    struct ifaddrs *ifaddr, *ifa;
    void *tmp_addr;
    int family;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        if ((version == IPV4 && family == AF_INET) ||
            (version == IPV6 && family == AF_INET6)) {
            
            if (strncmp(ifa->ifa_name, "lo", 2) == 0)
                continue;

            if (version == IPV4)
                tmp_addr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            else
                tmp_addr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;

            inet_ntop(family, tmp_addr, ip_buffer, buffer_size);
            break;
        }
    }

    freeifaddrs(ifaddr);
}

void *multicast_server_ip(void *arg) {
    ServerConfig *config = (ServerConfig *)arg;
    int multicast_sock;
    char ip_buffer[INET6_ADDRSTRLEN];

    if (config->version == IPV4) {
        struct sockaddr_in multicast_addr;
        
        if ((multicast_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("multicast socket creation failed");
            return NULL;
        }

        memset(&multicast_addr, 0, sizeof(multicast_addr));
        multicast_addr.sin_family = AF_INET;
        multicast_addr.sin_addr.s_addr = inet_addr(IPV4_MULTICAST_IP);
        multicast_addr.sin_port = htons(MULTICAST_PORT);

        // Set TTL for IPv4
        unsigned char ttl = 1;
        if (setsockopt(multicast_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
            perror("setsockopt IP_MULTICAST_TTL failed");
            close(multicast_sock);
            return NULL;
        }

        while (1) {
            get_local_ip(ip_buffer, sizeof(ip_buffer), IPV4);
            printf("Sending IPv4: %s\n", ip_buffer);

            if (sendto(multicast_sock, ip_buffer, strlen(ip_buffer)+1, 0,
                      (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0) {
                perror("sendto failed");
            }
            sleep(5);
        }
    } else {  // IPv6
        struct sockaddr_in6 multicast_addr;
        
        if ((multicast_sock = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
            perror("multicast socket creation failed");
            return NULL;
        }

        memset(&multicast_addr, 0, sizeof(multicast_addr));
        multicast_addr.sin6_family = AF_INET6;
        inet_pton(AF_INET6, IPV6_MULTICAST_IP, &multicast_addr.sin6_addr);
        multicast_addr.sin6_port = htons(MULTICAST_PORT);

        // Set hop limit for IPv6
        int hops = 1;
        if (setsockopt(multicast_sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops)) < 0) {
            perror("setsockopt IPV6_MULTICAST_HOPS failed");
            close(multicast_sock);
            return NULL;
        }

        while (1) {
            get_local_ip(ip_buffer, sizeof(ip_buffer), IPV6);
            printf("Sending IPv6: %s\n", ip_buffer);

            if (sendto(multicast_sock, ip_buffer, strlen(ip_buffer)+1, 0,
                      (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0) {
                perror("sendto failed");
            }
            sleep(5);
        }
    }

    close(multicast_sock);
    return NULL;
}


int draw_card() {
    return rand() % 11 + 1;
}

void save_rankings(const char *filename) {
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Failed to open rankings file for writing: %s", strerror(errno));
        return;
    }

    for (int i = 0; i < player_count; i++) {
        fprintf(file, "%s %d %d %d\n", players[i].name, players[i].wins, players[i].draws, players[i].losses);
    }

    fclose(file);
    syslog(LOG_INFO, "Rankings saved to file: %s", filename);
}

void load_rankings(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        if (errno != ENOENT) {
            syslog(LOG_ERR, "Failed to open rankings file for reading: %s", strerror(errno));
        }
        return;
    }

    player_count = 0;
    while (fscanf(file, "%49s %d %d %d", players[player_count].name, &players[player_count].wins, &players[player_count].draws, &players[player_count].losses) == 4) {
        player_count++;
        if (player_count >= MAX_PLAYERS) {
            break;
        }
    }

    fclose(file);
    syslog(LOG_INFO, "Rankings loaded from file: %s", filename);
}

void update_player_stats(const char *name, int result) {
    int found = 0;
    for (int i = 0; i < player_count; i++) {
        if (strcmp(players[i].name, name) == 0) {
            if (result == 1) players[i].wins++;
            else if (result == 0) players[i].draws++;
            else if (result == -1) players[i].losses++;
            found = 1;
            break;
        }
    }

    if (!found && player_count < MAX_PLAYERS) {
        strncpy(players[player_count].name, name, sizeof(players[player_count].name) - 1);
        players[player_count].wins = (result == 1);
        players[player_count].draws = (result == 0);
        players[player_count].losses = (result == -1);
        player_count++;
    }
}

void display_rankings(int connfd) {
    char buff[MAXLINE];
    snprintf(buff, sizeof(buff), "Current Rankings:\n");
    send(connfd, buff, strlen(buff), 0);

    for (int i = 0; i < player_count; i++) {
        snprintf(buff, sizeof(buff), "%s - W: %d, D: %d, L: %d\n", players[i].name, players[i].wins, players[i].draws, players[i].losses);
        send(connfd, buff, strlen(buff), 0);
    }
}

void handle_game(int connfd, const char *player_name) {
    char    buff[MAXLINE];
    int     player_score = 0;
    int     result = 0; // 1: win, 0: draw, -1: loss

    srand(time(0));

    int dealer_score = draw_card();
    snprintf(buff, sizeof(buff), "The dealer's face-up card is: %d\n", dealer_score);
    send(connfd, buff, strlen(buff), 0);

    while (1) {
        snprintf(buff, sizeof(buff), "Your current score: %d. Draw a card? (yes/no): \n", player_score);
        if (send(connfd, buff, strlen(buff), 0) < 0) {
            fprintf(stderr,"send error : %s\n", strerror(errno));
            break;
        }

        memset(buff, 0, sizeof(buff));
        if (recv(connfd, buff, sizeof(buff), 0) <= 0) {
            fprintf(stderr,"recv error : %s\n", strerror(errno));
            break;
        }

        if (strncmp(buff, "no", 2) == 0) {
            snprintf(buff, sizeof(buff), "Final score: %d.\n", player_score);
            send(connfd, buff, strlen(buff), 0);
            break;
        } else if (strncmp(buff, "yes", 3) == 0) {
            int card = draw_card(); 

            if(card == 1 || card == 11) {
                snprintf(buff,sizeof(buff), "You drew a %d. Do you want it to be 1 or 11? (1/11): \n", card);
                send(connfd,buff,sizeof(buff), 0);

                memset(buff, 0, sizeof(buff));
                if (recv(connfd, buff, sizeof(buff), 0) <= 0) {
                    fprintf(stderr,"recv error : %s\n", strerror(errno));
                    break;
                }

                int choice = atoi(buff);
                if(choice == 1) {
                    player_score += 1;
                } else if (choice == 11) {
                    player_score += 11;
                } else {
                    snprintf(buff,sizeof(buff), "Invalid choice. Defaulting to 1. \n");
                    send(connfd, buff, strlen(buff), 0);
                    player_score += 1;
                }    
            } else {
                player_score += card;
            }


            if (player_score == 21) {
                snprintf(buff, sizeof(buff), "You drew %d. Your total score is %d! BLACKJACK!\n", card, player_score);
                result = 1;
                send(connfd, buff, strlen(buff), 0);
                break;
            }
            else if (player_score > 21) {
                snprintf(buff, sizeof(buff), "You drew %d. Your total score is %d. BUST!\n", card, player_score);
                result = -1;
                send(connfd, buff, strlen(buff), 0);
                break;

            } else {
                snprintf(buff, sizeof(buff), "You drew %d. Your total score is now %d.\n", card, player_score);
                send(connfd, buff, strlen(buff), 0);
            }
        } else {
            snprintf(buff, sizeof(buff), "Invalid input. Please type 'yes' or 'no'.\n");
            send(connfd, buff, strlen(buff), 0);
        }
    }

    if (player_score <= 21) {
        while (dealer_score < 17) {
            int card = draw_card();
            dealer_score += card;
            snprintf(buff, sizeof(buff), "The dealer drew a %d. Dealer's score: %d.\n", card, dealer_score);
            send(connfd, buff, strlen(buff), 0);
        }

        if (dealer_score > 21) {
            snprintf(buff, sizeof(buff), "Dealer BUST! You win with a score of %d!\n", player_score);
            result = 1;
            send(connfd, buff, strlen(buff), 0);
        } else if (dealer_score > player_score) {
            snprintf(buff, sizeof(buff), "Dealer wins with a score of %d against your %d.\n", dealer_score, player_score);
            result = -1;
            send(connfd, buff, strlen(buff), 0);
        } else if (dealer_score < player_score) {
            snprintf(buff, sizeof(buff), "You win with a score of %d against the dealer's %d.\n", player_score, dealer_score);
            result = 1;
            send(connfd, buff, strlen(buff), 0);
        } else {
            snprintf(buff, sizeof(buff), "It's a tie! Both you and the dealer have a score of %d.\n", player_score);
            result = 0;
            send(connfd, buff, strlen(buff), 0);
        }
    }

    update_player_stats(player_name, result);
    save_rankings("/var/log/blackjack");

}

void handle_client(int connfd) {
    char buff[MAXLINE];
    char player_name[50];

    snprintf(buff, sizeof(buff), "Enter your name: ");
    send(connfd, buff, strlen(buff), 0);

    memset(player_name, 0, sizeof(player_name));
    if (recv(connfd, player_name, sizeof(player_name), 0) <= 0) {
        close(connfd);
        return;
    }

    printf("Player connected: %s\n", player_name);

    while (1) {
        snprintf(buff, sizeof(buff),
                 "Welcome, %s! Choose an option:\n"
                 "1. Play Blackjack\n"
                 "2. View Rankings\n"
                 "3. Exit\n"
                 "> ", player_name);
        send(connfd, buff, strlen(buff), 0);

        memset(buff, 0, sizeof(buff));
        if (recv(connfd, buff, sizeof(buff), 0) <= 0) {
            fprintf(stderr, "recv error : %s\n", strerror(errno));
            break;
        }

        if (strncmp(buff, "1", 1) == 0) {
            handle_game(connfd, player_name);
        } else if (strncmp(buff, "2", 1) == 0) {
            load_rankings("/var/log/blackjack");
            display_rankings(connfd);
        } else if (strncmp(buff, "3", 1) == 0) {
            snprintf(buff, sizeof(buff), "Goodbye, %s!\n", player_name);
            send(connfd, buff, strlen(buff), 0);
            break;
        } else {
            snprintf(buff, sizeof(buff), "Invalid option. Please try again.\n");
            send(connfd, buff, strlen(buff), 0);
        }
    }

    save_rankings("/var/log/blackjack");
    close(connfd);
    printf("Player %s disconnected.\n", player_name);
}

int
daemon_init(const char *pname, int facility, uid_t uid)
{
    int		i;
    pid_t	pid;

    if ( (pid = fork()) < 0)
        return (-1);
    else if (pid)
        exit(0);			/* parent terminates */

    /* child 1 continues... */

    if (setsid() < 0)			/* become session leader */
        return (-1);

    signal(SIGHUP, SIG_IGN);
    if ( (pid = fork()) < 0)
        return (-1);
    else if (pid)
        exit(0);			/* child 1 terminates */

    /* child 2 continues... */

    chdir("/");				/* change working directory - or chroot()*/

    /* close off file descriptors */
    for (i = 0; i < MAXFD; i++){
        close(i);
    }

    /* redirect stdin, stdout, and stderr to /dev/null */
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);

    openlog(pname, LOG_PID, facility);
    
    setuid(uid); /* change user */
    
    return (0);				/* success */
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf (stderr, "Usage: %s <ip version> \n", argv[0]);
        return 1;
    }

    if (daemon_init("blackjackd", LOG_DAEMON, 1000) < 0) {
        fprintf(stderr, "Failed to initialize daemon.\n");
        return 1;
    }

    syslog(LOG_INFO, "Blackjack server started");

    int listenfd, connfd;
    struct sockaddr_in server_addr;
    struct sockaddr_in6 server_addr6;
    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t multicast_thread;
    ServerConfig config;
  
    int choice = atoi(argv[1]);
    config.version = (choice == 6) ? IPV6 : IPV4;

    if (config.version == IPV4) {
        if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            fprintf(stderr, "socket error : %s\n", strerror(errno));
            return 1;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        server_addr.sin_port = htons(PORT);

        if (bind(listenfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
            fprintf(stderr, "bind error : %s\n", strerror(errno));
            return 1;
        }
    } else {
        if ((listenfd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
            fprintf(stderr, "socket error : %s\n", strerror(errno));
            return 1;
        }

        server_addr6.sin6_family = AF_INET6;
        server_addr6.sin6_addr = in6addr_any;
        server_addr6.sin6_port = htons(PORT);

        if (bind(listenfd, (struct sockaddr *) &server_addr6, sizeof(server_addr6)) < 0) {
            fprintf(stderr, "bind error : %s\n", strerror(errno));
            return 1;
        }
    }

    if (listen(listenfd, MAX_PLAYERS) < 0) {
        fprintf(stderr, "listen error : %s\n", strerror(errno));
        return 1;
    }

    if (pthread_create(&multicast_thread, NULL, multicast_server_ip, &config) != 0) {
        fprintf(stderr, "Failed to create multicast thread\n");
        return 1;
    }

    printf("Server listening on port %d using %s...\n", 
           PORT, config.version == IPV4 ? "IPv4" : "IPv6");

    signal(SIGPIPE, SIG_IGN);

    while (1) {
        connfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_len);
        if (connfd < 0) {
            fprintf(stderr, "accept error : %s\n", strerror(errno));
            continue;
        }

        if (fork() == 0) {
            close(listenfd);
            handle_client(connfd);
            exit(0);
        }
        close(connfd);
    }

    close(listenfd);
    return 0;
}
