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

typedef struct client {
    int fd;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    struct client* next;
} client_t;

void die(int ret, const char* msg) {
    if (ret < 0) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

client_t* add_client(client_t* tete, int fd, struct sockaddr_storage* addr, socklen_t addr_len) {
    client_t* nouveau = malloc(sizeof(client_t));
    if (!nouveau) {
        perror("malloc()");
        exit(EXIT_FAILURE);
    }
    nouveau->fd = fd;
    nouveau->addr = *addr;
    nouveau->addr_len = addr_len;
    nouveau->next = tete;
    return nouveau;
}

client_t* remove_client(client_t* tete, int fd) {
    client_t* actuel = tete;
    client_t* precedent = NULL;

    while (actuel != NULL) {
        if (actuel->fd == fd) {
            if (precedent == NULL) {
                tete = actuel->next;
            } else {
                precedent->next = actuel->next;
            }
            free(actuel);
            return tete;
        }
        precedent = actuel;
        actuel = actuel->next;
    }
    return tete;
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

int read_from_socket(int fd, void* buf, int size) {
    int size_received = 0;
    while (size_received < size) {
        int ret_value = read(fd, (char*)buf + size_received, size - size_received);
        if (ret_value <= 0) {
            return ret_value; 
        }
        size_received += ret_value;
    }
    return size_received;
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

    client_t* liste_clients = NULL;

    printf("Serveur en ecoute sur le port %s...\n", server_port);

    while (1) {
        int nbfds = poll(fds, FD_TAB_SIZE, -1);
        if (nbfds < 0) break;

        if (fds[0].revents & POLLIN) {
            struct sockaddr_storage client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
            
            if (new_fd != -1) {
                int added = 0;
                for (int j = 1; j < FD_TAB_SIZE; j++) {
                    if (fds[j].fd == -1) {
                        fds[j].fd = new_fd;
                        fds[j].events = POLLIN;
                        
                        liste_clients = add_client(liste_clients, new_fd, &client_addr, client_len);
                        
                        char host[NI_MAXHOST], serv[NI_MAXSERV];
                        getnameinfo((struct sockaddr*)&client_addr, client_len, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
                        printf("Nouveau client connecte depuis %s:%s sur le fd %d\n", host, serv, new_fd);
                        
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
                int msg_size = 0;
                
                int ret = read_from_socket(fds[i].fd, &msg_size, sizeof(int));
                
                if (ret <= 0) {
                    printf("Client déconnecté sur le fd %d\n", fds[i].fd);
                    liste_clients = remove_client(liste_clients, fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1; 
                } else {
                    char buf[MSG_LEN];
                    memset(buf, 0, MSG_LEN);
                    
                    if (msg_size > 0 && msg_size < MSG_LEN) {
                        ret = read_from_socket(fds[i].fd, buf, msg_size);
                        if (ret <= 0) {
                            liste_clients = remove_client(liste_clients, fds[i].fd);
                            close(fds[i].fd);
                            fds[i].fd = -1;
                            fds[i].revents = 0;
                            continue;
                        }
                        buf[msg_size] = '\0';
                        
                        if(strcmp("/quit", buf) == 0) {
                            printf("client deconnecte sur le fd %d\n", fds[i].fd);
                            liste_clients = remove_client(liste_clients, fds[i].fd);
                            close(fds[i].fd);
                            fds[i].fd = -1;
                            fds[i].revents = 0;
                            continue;
                        }

                        printf("Reçu du fd %d (taille %d): %s", fds[i].fd, msg_size, buf);

                        if (send(fds[i].fd, &msg_size, sizeof(int), 0) <= 0 ||
                            send(fds[i].fd, buf, msg_size, 0) <= 0) {
                            perror("send()");
                        }
                    }
                }
                fds[i].revents = 0;
            }
        }
    }

    while (liste_clients != NULL) {
        client_t* temp = liste_clients;
        liste_clients = liste_clients->next;
        close(temp->fd);
        free(temp);
    }

    close(listen_fd);
    return 0;
}