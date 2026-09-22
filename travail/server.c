#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <netdb.h>

#include "common.h"

#define FD_TAB_SIZE 128

void die(int ret, const char* msg) {
    if (ret < 0) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

void echo_server(int sockfd) {
    char buff[MSG_LEN];
    while (1) {
        memset(buff, 0, MSG_LEN);
        if (recv(sockfd, buff, MSG_LEN, 0) <= 0) {
            break;
        }
        printf("Received: %s", buff);
        if (send(sockfd, buff, strlen(buff), 0) <= 0) {
            break;
        }
        printf("Message envoye!\n");
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char* server_port = argv[1];

    struct addrinfo hints, *result, *rp;
    int listen_fd;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, server_port, &hints, &result) != 0) {
        perror("getaddrinfo()");
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd == -1) continue;

        int yes = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(listen_fd);
    }

    if (rp == NULL) {
        fprintf(stderr, "Could not bind\n");
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(result);

    int ret_value = listen(listen_fd, SOMAXCONN);
    die(ret_value, "Listening");

    struct pollfd fds[FD_TAB_SIZE];
    for (int i = 0; i < FD_TAB_SIZE; i++) {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }

    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;

    printf("Serveur en ecoute sur le port %s...\n", server_port);

    while (1) {
        int nbfds = poll(fds, FD_TAB_SIZE, -1);
        if (nbfds < 0) break;

        if (fds[0].revents & POLLIN) {
            int new_fd = accept(listen_fd, NULL, NULL);
            if (new_fd != -1) {
                int added = 0;
                for (int j = 1; j < FD_TAB_SIZE; j++) {
                    if (fds[j].fd == -1) {
                        fds[j].fd = new_fd;
                        fds[j].events = POLLIN;
                        printf("Nouveau client connecte sur le fd %d\n", new_fd);
                        added = 1;
                        break;
                    }
                }
                if (!added) {
                    printf("Serveur plein, rejet du client.\n");
                    close(new_fd);
                }
            }
            fds[0].revents = 0;
        }

        for (int i = 1; i < FD_TAB_SIZE; i++) {
            if (fds[i].fd != -1 && (fds[i].revents & POLLIN)) {
                char buf[MSG_LEN];
                memset(buf, 0, MSG_LEN);
                int ret = recv(fds[i].fd, buf, MSG_LEN, 0);
                
                if (ret <= 0) {
                    printf("Client déconnecté sur le fd %d\n", fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1; 
                } else {
                    printf("Reçu du fd %d: %s", fds[i].fd, buf);
                    if (send(fds[i].fd, buf, ret, 0) <= 0) {
                        perror("send()");
                    }
                }
                fds[i].revents = 0;
            }
        }
    }

    close(listen_fd);
    return 0;
}