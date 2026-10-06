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
#include "msg_struct.h"

void echo_client(int sockfd) {
    struct pollfd fds[2];
    
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    char buff[MSG_LEN];
    printf("Message: ");
    fflush(stdout);

    char my_pseudo[NICK_LEN];
    memset(my_pseudo, 0, NICK_LEN);

    char pseudo_emetteur_fichier[NICK_LEN]; 
    memset(pseudo_emetteur_fichier, 0, NICK_LEN);
    int en_attente_reponse_fichier = 0; 
    char chemin_fichier_local[MSG_LEN]; 
    memset(chemin_fichier_local, 0, MSG_LEN);
    // -------------------------

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

            // REQ 3.2 
            if (en_attente_reponse_fichier && (buff[0] == 'Y' || buff[0] == 'y' || buff[0] == 'N' || buff[0] == 'n') && (buff[1] == '\0' || buff[1] == '\n')) {
                
                if (buff[0] == 'Y' || buff[0] == 'y') {
                    msg.type = FILE_ACCEPT;
                    strncpy(msg.infos, pseudo_emetteur_fichier, INFOS_LEN - 1); 
                    
    
                    payload_a_envoyer = "127.0.0.1:8080"; 
                    msg.pld_len = strlen(payload_a_envoyer);
                } else {
                    msg.type = FILE_REJECT;
                    strncpy(msg.infos, pseudo_emetteur_fichier, INFOS_LEN - 1); 
                    msg.pld_len = 0;
                }
                
                en_attente_reponse_fichier = 0; 
            }
            else if (en_attente_reponse_fichier) {
                printf("Veuillez repondre par Y ou N.\n");
                fflush(stdout);
                continue;
            }

            else if (strncmp(buff, "/nick ", 6) == 0) {
                char* pseudo = buff + 6;
                pseudo[strcspn(pseudo, "\n")] = '\0';

                if (strlen(pseudo) >= NICK_LEN) {
                    printf("Pseudo trop long.\n");
                    printf("Message: ");
                    fflush(stdout);
                    continue;
                }

                msg.type = NICKNAME_NEW;
                msg.pld_len = 0;
                strncpy(msg.infos, buff + 6, INFOS_LEN - 1);
                msg.infos[strcspn(msg.infos, "\n")] = 0;
            }
            else if (strcmp(buff, "/who\n") == 0) {
                msg.type = NICKNAME_LIST;
                msg.pld_len = 0;
            }
            else if (strncmp(buff, "/whois ", 7) == 0) {
                msg.type = NICKNAME_INFOS;
                msg.pld_len = 0;
                strncpy(msg.infos, buff + 7, INFOS_LEN - 1);
                msg.infos[strcspn(msg.infos, "\n")] = 0;
            }
            else if (strncmp(buff, "/msgall ", 8) == 0) {
                msg.type = BROADCAST_SEND;
                payload_a_envoyer = buff + 8;
                msg.pld_len = strlen(payload_a_envoyer);
            }
            else if (strncmp(buff, "/msg ", 5) == 0) {
                char* destinataire = buff + 5;
                char* message = NULL;

                for (int j = 0; destinataire[j] != '\0'; j++) {
                    if (destinataire[j] == ' ') {
                        destinataire[j] = '\0';
                        message = destinataire + j + 1;
                        break;
                    }
                }

                if (message == NULL) {
                    printf("Usage: /msg <pseudo> <message>\n");
                    printf("Message: ");
                    fflush(stdout);
                    continue;
                }

                msg.type = UNICAST_SEND;
                strncpy(msg.infos, destinataire, INFOS_LEN - 1);
                payload_a_envoyer = message;
                msg.pld_len = strlen(payload_a_envoyer);
            }
            else if (strncmp(buff, "/send ", 6) == 0) {
                char* destinataire = buff + 6;
                char* chemin_fichier = NULL;

                for (int j = 0; destinataire[j] != '\0'; j++) {
                    if (destinataire[j] == ' ') {
                        destinataire[j] = '\0'; 
                        chemin_fichier = destinataire + j + 1; 
                        break; 
                    }
                }

                if (chemin_fichier == NULL) {
                    printf("Usage: /send <pseudo> <chemin_fichier>\nMessage: ");
                    fflush(stdout);
                    continue;
                }

                if (chemin_fichier[0] == '"') {
                    chemin_fichier++; 
                    for (int j = 0; chemin_fichier[j] != '\0'; j++) {
                        if (chemin_fichier[j] == '"') {
                            chemin_fichier[j] = '\0'; 
                        }
                    }
                }
                
                for (int j = 0; chemin_fichier[j] != '\0'; j++) {
                    if (chemin_fichier[j] == '\n') {
                        chemin_fichier[j] = '\0';
                        break;
                    }
                }

                char* nom_fichier = chemin_fichier; 
                for (int j = 0; chemin_fichier[j] != '\0'; j++) {
                    if (chemin_fichier[j] == '/') {
                        nom_fichier = chemin_fichier + j + 1; 
                    }
                }

                strncpy(chemin_fichier_local, chemin_fichier, MSG_LEN - 1);

                msg.type = FILE_REQUEST;
                strncpy(msg.infos, destinataire, INFOS_LEN - 1); 
                payload_a_envoyer = nom_fichier; 
                msg.pld_len = strlen(payload_a_envoyer);
            }
            else {
                msg.type = ECHO_SEND;
                msg.pld_len = strlen(buff);
            }

            if (send(sockfd, &msg, sizeof(struct message), 0) <= 0) break;
            if (msg.pld_len > 0) {
                if (send(sockfd, payload_a_envoyer, msg.pld_len, 0) <= 0) break;
            }

            if (msg.type == UNICAST_SEND || msg.type == BROADCAST_SEND || msg.type == FILE_REQUEST) {
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

                if (reponse_msg.type == NICKNAME_NEW && strstr(payload, "Welcome on the chat")) {
                    char* espace = NULL;
                    for (int j = 0; payload[j] != '\0'; j++) {
                        if (payload[j] == ' ') espace = payload + j;
                    }
                    if (espace) {
                        strncpy(my_pseudo, espace + 1, NICK_LEN - 1);
                        my_pseudo[strcspn(my_pseudo, "\n")] = 0;
                    }
                }
            }

            if (reponse_msg.type == FILE_REQUEST) {
                printf("\n%s wants you to accept the transfer of the file named \"%s\". Do you accept? [Y/N]\n", 
                       reponse_msg.nick_sender, payload);
                
                strncpy(pseudo_emetteur_fichier, reponse_msg.nick_sender, NICK_LEN - 1);
                en_attente_reponse_fichier = 1;
            }
            else if (reponse_msg.type == FILE_ACCEPT) {
                printf("\n%s accepted file transfer.\n", reponse_msg.nick_sender);
                printf("Connecting to %s and sending the file...\n", reponse_msg.nick_sender);
            }
            else if (reponse_msg.type == FILE_REJECT) {
                printf("\n%s cancelled file transfer.\n", reponse_msg.nick_sender);
            }
            else if (reponse_msg.type == UNICAST_SEND || reponse_msg.type == BROADCAST_SEND) {
                printf("\n[%s] : %s", reponse_msg.nick_sender, payload);
            }
            else {
                if (payload) printf("\n%s", payload);
            }

            if (payload) free(payload);

            if (!en_attente_reponse_fichier) {
                printf("Message: ");
            }
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