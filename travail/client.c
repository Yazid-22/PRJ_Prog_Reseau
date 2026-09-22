#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include "common.h"

void echo_client(int sockfd) {
    struct pollfd fds[2];
    
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    char buff[MSG_LEN];
    printf("Message: ");

    while (1) {
        int nbfds = poll(fds, 2, -1);
        if (nbfds < 0) {
            perror("poll()");
            break;
        }

        if (fds[0].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);
            int n = 0;
            
            while ((buff[n++] = getchar()) != '\n') {
                if (n >= MSG_LEN - 1) {
                    break;
                }
            }
            buff[n] = '\0';
            if(strcmp("/quit",buff) == 0)
            {
                int size = strlen(buff);
                send(sockfd, &size, sizeof(int), 0);
                send(sockfd, buff, size, 0);
                printf("demande de deconnexion... \n");
                break;
            }
            
            int size = strlen(buff);
            if (size > 0) {
                if (send(sockfd, &size, sizeof(int), 0) <= 0) {
                    break;
                }
                if (send(sockfd, buff, size, 0) <= 0) {
                    break;
                }
                printf("Envoi du message reussi!\n");
            }
            
            printf("Message: ");
            fds[0].revents = 0;
        }

        if (fds[1].revents & POLLIN) {
            int response_size = 0;
            if (recv(sockfd, &response_size, sizeof(int), 0) <= 0) {
                printf("\nServeur déconnecté.\n");
                break;
            }

            memset(buff, 0, MSG_LEN);
            int total_received = 0;
            while (total_received < response_size) {
                int ret = recv(sockfd, buff + total_received, response_size - total_received, 0);
                if (ret <= 0) {
                    break;
                }
                total_received += ret;
            }
            buff[total_received] = '\0';
            
            printf("\nReception du message: %s", buff);
            printf("Message: ");
            fds[1].revents = 0;
        }
    }
}

int handle_connect(const char* host, const char* port) {
    struct addrinfo hints, *result, *rp;
    int sfd;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &result) != 0) {
        perror("getaddrinfo()");
        exit(EXIT_FAILURE);
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }
        close(sfd);
    }
    if (rp == NULL) {
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(result);
    return sfd;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "pas de numero de port et de serveur indique\n");
        exit(EXIT_FAILURE);
    }
    int sfd;
    const char* server_name = argv[1];
    const char* server_port = argv[2];
    sfd = handle_connect(server_name, server_port);
    echo_client(sfd);
    close(sfd);
    return EXIT_SUCCESS;
}