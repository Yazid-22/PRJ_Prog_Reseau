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
#include "msg_struct.h" // Req 2.0

void echo_client(int sockfd) {
    struct pollfd fds[2];
    
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    char buff[MSG_LEN];
    printf("Message: ");
    fflush(stdout);

    // On stocke le pseudo actuel du client
    char my_pseudo[NICK_LEN];
    memset(my_pseudo, 0, NICK_LEN);

    while (1) {
        int nbfds = poll(fds, 2, -1);
        if (nbfds < 0) break;

        if (fds[0].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);
            int n = 0;
            
            while ((buff[n++] = getchar()) != '\n') {
                if (n >= MSG_LEN - 1) break;
            }
            buff[n] = '\0';
            
            if (strncmp("/quit", buff, 5) == 0) {
                printf("demande de deconnexion... \n");
                break;
            }

            struct message msg;
            memset(&msg, 0, sizeof(struct message));
            strcpy(msg.nick_sender, my_pseudo);

            char* payload_a_envoyer = buff;

            // Req 2.1 : Parser /nick
            if (strncmp(buff, "/nick ", 6) == 0) {
                msg.type = NICKNAME_NEW;
                msg.pld_len = 0;
                strncpy(msg.infos, buff + 6, INFOS_LEN - 1);
                msg.infos[strcspn(msg.infos, "\n")] = 0;
            }

            // Req 2.5 : /who
            else if (strcmp(buff, "/who\n") == 0) {
                msg.type = NICKNAME_LIST;
                msg.pld_len = 0;
            }

            // Req 2.6 : /whois <pseudo>
            else if (strncmp(buff, "/whois ", 7) == 0) {
                msg.type = NICKNAME_INFOS;
                msg.pld_len = 0;
                strncpy(msg.infos, buff + 7, INFOS_LEN - 1);
                msg.infos[strcspn(msg.infos, "\n")] = 0;
            }

            // Req 2.7 : /msgall <message>
            else if (strncmp(buff, "/msgall ", 8) == 0) {
                msg.type = BROADCAST_SEND;

                payload_a_envoyer = buff + 8;
                msg.pld_len = strlen(payload_a_envoyer);
            }

            // Req 2.9 : /msg <pseudo> <message>
            else if (strncmp(buff, "/msg ", 5) == 0) {

                char* destinataire = buff + 5;
                char* message = strchr(destinataire, ' ');

                if (message == NULL) {
                    printf("Usage: /msg <pseudo> <message>\n");
                    printf("Message: ");
                    fflush(stdout);
                    continue;
                }

                *message = '\0';
                message++;

                msg.type = UNICAST_SEND;

                strncpy(msg.infos, destinataire, INFOS_LEN - 1);

                payload_a_envoyer = message;
                msg.pld_len = strlen(payload_a_envoyer);
            }

            // Message normal
            else {
                msg.type = ECHO_SEND;
                msg.pld_len = strlen(buff);
                }

            // Envoi : structure puis payload (Req 2.0)
            if (send(sockfd, &msg, sizeof(struct message), 0) <= 0) break;
            if (msg.pld_len > 0) {
                if (send(sockfd, payload_a_envoyer, msg.pld_len, 0) <= 0) break;
            }

            if (msg.type == UNICAST_SEND || msg.type == BROADCAST_SEND) {
                printf("Message: ");
                fflush(stdout);
            }
            
            fds[0].revents = 0;
        }

        if (fds[1].revents & POLLIN) {
            struct message reponse_msg;
            if (recv(sockfd, &reponse_msg, sizeof(struct message), 0) <= 0) {
                printf("\nServeur déconnecté.\n");
                break;
            }

            char* payload = NULL;
            if (reponse_msg.pld_len > 0) {
                payload = malloc(reponse_msg.pld_len + 1);
                int total_received = 0;
                while (total_received < reponse_msg.pld_len) {
                    int ret = recv(sockfd, payload + total_received, reponse_msg.pld_len - total_received, 0);
                    if (ret <= 0) break;
                    total_received += ret;
                }
                payload[reponse_msg.pld_len] = '\0';
                
                // Si la commande était un NICKNAME_NEW validé, on met à jour notre pseudo local (bonus d'affichage)
                if (reponse_msg.type == NICKNAME_NEW && strstr(payload, "Welcome on the chat")) {
                    // On récupère le dernier mot du message (qui est le pseudo validé)
                    char* espace = strrchr(payload, ' ');
                    if (espace) {
                        strncpy(my_pseudo, espace + 1, NICK_LEN - 1);
                        my_pseudo[strcspn(my_pseudo, "\n")] = 0;
                    }
                }
                
                if (reponse_msg.type == UNICAST_SEND ||
                    reponse_msg.type == BROADCAST_SEND) {

                    printf("\n[%s] : %s",
                        reponse_msg.nick_sender,
                        payload);
                }
                else {
                    printf("\n%s", payload);
                }
                free(payload);
            }

            printf("Message: ");
            fflush(stdout);
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
    if (getaddrinfo(host, port, &hints, &result) != 0) exit(EXIT_FAILURE);
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) break;
        close(sfd);
    }
    freeaddrinfo(result);
    return sfd;
}

int main(int argc, char* argv[]) {
    if (argc != 3) exit(EXIT_FAILURE);
    echo_client(handle_connect(argv[1], argv[2]));
    return EXIT_SUCCESS;
}