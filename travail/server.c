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
#include <ctype.h>
#include <time.h>

#include "common.h"
#include "msg_struct.h"

#define FD_TAB_SIZE 128

// Req 2.3 : Mise à jour de la structure client avec pseudo et date de co
typedef struct client {
    int fd;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    char pseudo[NICK_LEN];
    time_t date_co;
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
    memset(nouveau->pseudo, 0, NICK_LEN); // Pseudo vide au départ
    time(&(nouveau->date_co)); // Date de connexion
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

client_t* get_client_by_fd(client_t* tete, int fd) {
    client_t* actuel = tete;
    while (actuel != NULL) {
        if (actuel->fd == fd) return actuel;
        actuel = actuel->next;
    }
    return NULL;
}

client_t* get_client_by_pseudo(client_t* tete, const char* pseudo) {
    client_t* actuel = tete;

    while (actuel != NULL) {
        if (strcmp(actuel->pseudo, pseudo) == 0) {
            return actuel;
        }

        actuel = actuel->next;
    }

    return NULL;
}

// Req 2.2 : Vérifier si un pseudo est déjà utilisé
int pseudo_existe(client_t* tete, const char* pseudo) {
    client_t* actuel = tete;
    while (actuel != NULL) {
        if (strcmp(actuel->pseudo, pseudo) == 0) return 1;
        actuel = actuel->next;
    }
    return 0;
}

// Req 2.1 : Valider alphanumérique
int est_pseudo_valide(const char* pseudo) {
    if (strlen(pseudo) == 0 || strlen(pseudo) >= NICK_LEN) return 0;
    for (int i = 0; pseudo[i] != '\0'; i++) {
        if (!isalnum(pseudo[i])) return 0;
    }
    return 1;
}

int read_from_socket(int fd, void* buf, int size) {
    int size_received = 0;
    while (size_received < size) {
        int ret_value = read(fd, (char*)buf + size_received, size - size_received);
        if (ret_value <= 0) return ret_value;
        size_received += ret_value;
    }
    return size_received;
}

void gerer_nouvelle_connexion(int listen_fd, struct pollfd* fds, client_t** liste_clients) {
    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);
    int new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    
    if (new_fd != -1) {
        int ajoute = 0;
        for (int j = 1; j < FD_TAB_SIZE; j++) {
            if (fds[j].fd == -1) {
                fds[j].fd = new_fd;
                fds[j].events = POLLIN;
                *liste_clients = add_client(*liste_clients, new_fd, &client_addr, client_len);
                ajoute = 1;
                break;
            }
        }
        if (!ajoute) {
            close(new_fd);
        }
    }
    fds[0].revents = 0;
}

void gerer_donnees_client(struct pollfd* fds, int i, client_t** liste_clients) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));

    // Req 2.0 : Lire la structure
    int ret = read_from_socket(fds[i].fd, &msg, sizeof(struct message));
    
    if (ret <= 0) {
        *liste_clients = remove_client(*liste_clients, fds[i].fd);
        close(fds[i].fd);
        fds[i].fd = -1;
        return;
    }

    // Req 2.0 : Lire le payload éventuel
    char* payload = NULL;
    if (msg.pld_len > 0) {
        payload = malloc(msg.pld_len + 1);
        ret = read_from_socket(fds[i].fd, payload, msg.pld_len);
        if (ret <= 0) {
            free(payload);
            *liste_clients = remove_client(*liste_clients, fds[i].fd);
            close(fds[i].fd);
            fds[i].fd = -1;
            return;
        }
        payload[msg.pld_len] = '\0';
    }

    client_t* expediteur = get_client_by_fd(*liste_clients, fds[i].fd);
    if (!expediteur) {
        if(payload) free(payload);
        return;
    }

    struct message reponse_msg;
    memset(&reponse_msg, 0, sizeof(struct message));
    char reponse_txt[MSG_LEN];
    memset(reponse_txt, 0, MSG_LEN);

    // Req 2.1, 2.2, 2.4 : Gestion de NICKNAME_NEW
    if (msg.type == NICKNAME_NEW) {
        if (!est_pseudo_valide(msg.infos)) {
            sprintf(reponse_txt, "[Serveur] : Pseudo invalide (lettres et chiffres uniquement).\n");
        } else if (pseudo_existe(*liste_clients, msg.infos)) {
            sprintf(reponse_txt, "[Serveur] : Erreur, ce pseudo est deja utilise.\n");
        } else {
            strcpy(expediteur->pseudo, msg.infos);
            sprintf(reponse_txt, "[Serveur] : Welcome on the chat %s\n", expediteur->pseudo);
        }
        
        reponse_msg.type = NICKNAME_NEW;
        reponse_msg.pld_len = strlen(reponse_txt);
        send(fds[i].fd, &reponse_msg, sizeof(struct message), 0);
        send(fds[i].fd, reponse_txt, reponse_msg.pld_len, 0);
    }

    // Req 2.5 : /who
    else if (msg.type == NICKNAME_LIST) {
        reponse_msg.type = NICKNAME_LIST;

        strcpy(reponse_txt, "[Serveur] : Online users are\n");

        client_t* actuel = *liste_clients;

        while (actuel != NULL) {
            if (actuel->pseudo[0] != '\0') {
                strcat(reponse_txt, " - ");
                strcat(reponse_txt, actuel->pseudo);
                strcat(reponse_txt, "\n");
            }

            actuel = actuel->next;
        }

        reponse_msg.pld_len = strlen(reponse_txt);

        send(fds[i].fd, &reponse_msg, sizeof(struct message), 0);
        send(fds[i].fd, reponse_txt, reponse_msg.pld_len, 0);
    }

    // Req 2.6 : /whois <pseudo>
    else if (msg.type == NICKNAME_INFOS) {

        reponse_msg.type = NICKNAME_INFOS;

        client_t* cible = get_client_by_pseudo(*liste_clients, msg.infos);

        if (cible == NULL) {
            sprintf(reponse_txt,
                    "[Serveur] : utilisateur %s introuvable.\n",
                    msg.infos);
        }   else {
                char host[NI_MAXHOST];
                char port[NI_MAXSERV];
                char date[64];

                getnameinfo((struct sockaddr*)&cible->addr,
                    cible->addr_len,
                    host, sizeof(host),
                    port, sizeof(port),
                    NI_NUMERICHOST | NI_NUMERICSERV);

                struct tm* info_date = localtime(&cible->date_co);

                strftime(date, sizeof(date),
                    "%Y/%m/%d@%H:%M",
                    info_date);

                sprintf(reponse_txt,
                    "[Serveur] : %s connected since %s with IP address %s and port number %s\n",
                    cible->pseudo,
                    date,
                    host,
                    port);
        }

        reponse_msg.pld_len = strlen(reponse_txt);

        send(fds[i].fd,
            &reponse_msg,
            sizeof(struct message),
            0);

        send(fds[i].fd,
            reponse_txt,
            reponse_msg.pld_len,
            0);
    }


    // Req 2.11 : Echo pour tester (en attendant les requêtes msg/msgall)
    else if (msg.type == ECHO_SEND) {
        reponse_msg.type = ECHO_SEND;
        reponse_msg.pld_len = msg.pld_len;
        send(fds[i].fd, &reponse_msg, sizeof(struct message), 0);
        if (payload) send(fds[i].fd, payload, msg.pld_len, 0);
    }

    if (payload) free(payload);
    fds[i].revents = 0;
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

    freeaddrinfo(result);

    listen(listen_fd, SOMAXCONN);

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
            gerer_nouvelle_connexion(listen_fd, fds, &liste_clients);
        }

        for (int i = 1; i < FD_TAB_SIZE; i++) {
            if (fds[i].fd != -1 && (fds[i].revents & POLLIN)) {
                gerer_donnees_client(fds, i, &liste_clients);
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