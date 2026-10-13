#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <sys/stat.h>

#include "common.h"
#include "msg_struct.h"


int lire_exactement(int fd, void* buf, int size) {
    int octets_recus = 0;
    while (octets_recus < size) {
        int ret = read(fd, (char*)buf + octets_recus, size - octets_recus);
        if (ret <= 0) return ret;
        octets_recus += ret;
    }
    return octets_recus;
}

void recevoir_fichier_p2p(int sockfd_serveur, char* pseudo_emetteur, char* nom_fichier_attendu) {
    
    int p2p_ecoute_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in adresse_p2p;
    memset(&adresse_p2p, 0, sizeof(adresse_p2p));
    adresse_p2p.sin_family = AF_INET;
    adresse_p2p.sin_addr.s_addr = inet_addr("127.0.0.1");
    adresse_p2p.sin_port = 0; // Port 0 = port libre aléatoire

    bind(p2p_ecoute_fd, (struct sockaddr*)&adresse_p2p, sizeof(adresse_p2p));
    socklen_t taille_adresse = sizeof(adresse_p2p);
    getsockname(p2p_ecoute_fd, (struct sockaddr*)&adresse_p2p, &taille_adresse); 
    listen(p2p_ecoute_fd, 1);

    // 2. On prévient l'émetteur via le serveur qu'on est pret
    struct message msg_accept;
    memset(&msg_accept, 0, sizeof(struct message));
    msg_accept.type = FILE_ACCEPT;
    strncpy(msg_accept.infos, pseudo_emetteur, INFOS_LEN - 1); 
    
    char ip_port_str[64];
    sprintf(ip_port_str, "127.0.0.1:%d", ntohs(adresse_p2p.sin_port)); 
    msg_accept.pld_len = strlen(ip_port_str);

    send(sockfd_serveur, &msg_accept, sizeof(struct message), 0);
    send(sockfd_serveur, ip_port_str, msg_accept.pld_len, 0);

    
    printf("Receiving the file from %s...\n", pseudo_emetteur);
    int p2p_donnees_fd = accept(p2p_ecoute_fd, NULL, NULL);

    mkdir(".resa1", 0777);
    mkdir(".resa1/inbox", 0777);
    char chemin_final[512];
    sprintf(chemin_final, ".resa1/inbox/%s", nom_fichier_attendu);

    FILE* fichier_recu = fopen(chemin_final, "wb");
    if (fichier_recu) {
        while (1) {
            struct message msg_p2p;
            int r = lire_exactement(p2p_donnees_fd, &msg_p2p, sizeof(struct message));
            if (r <= 0) break;

            if (msg_p2p.type == FILE_SEND) {
                if (msg_p2p.pld_len == 0) break; // on a atteint la fin du fichier
                
                char* morceau = malloc(msg_p2p.pld_len);
                lire_exactement(p2p_donnees_fd, morceau, msg_p2p.pld_len);
                fwrite(morceau, 1, msg_p2p.pld_len, fichier_recu); 
                free(morceau);
            }
        }
        fclose(fichier_recu);
        printf("%s saved in %s\n", nom_fichier_attendu, chemin_final);

        struct message ack_msg;
        memset(&ack_msg, 0, sizeof(struct message));
        ack_msg.type = FILE_ACK;
        strncpy(ack_msg.infos, nom_fichier_attendu, INFOS_LEN - 1);
        ack_msg.pld_len = 0;
        send(p2p_donnees_fd, &ack_msg, sizeof(struct message), 0);
    } else {
        printf("Erreur : impossible de créer le fichier %s\n", chemin_final);
    }
    close(p2p_donnees_fd);
    close(p2p_ecoute_fd);
}

void envoyer_fichier_p2p(char* payload_ip_port, char* chemin_fichier_local, char* pseudo_destinataire) {
    char ip_cible[64];
    char port_cible[64];
    int index_deux_points = -1;
    
    for (int j = 0; payload_ip_port[j] != '\0'; j++) {
        if (payload_ip_port[j] == ':') {
            index_deux_points = j;
            break;
        }
    }

    if (index_deux_points != -1) {
        for (int j = 0; j < index_deux_points; j++) ip_cible[j] = payload_ip_port[j];
        ip_cible[index_deux_points] = '\0';

        int k = 0;
        for (int j = index_deux_points + 1; payload_ip_port[j] != '\0'; j++) port_cible[k++] = payload_ip_port[j];
        port_cible[k] = '\0';

        struct addrinfo hints_p2p, *result_p2p;
        memset(&hints_p2p, 0, sizeof(struct addrinfo));
        hints_p2p.ai_family = AF_INET;
        hints_p2p.ai_socktype = SOCK_STREAM;
        getaddrinfo(ip_cible, port_cible, &hints_p2p, &result_p2p);
        
        int p2p_donnees_fd = socket(result_p2p->ai_family, result_p2p->ai_socktype, result_p2p->ai_protocol);
        connect(p2p_donnees_fd, result_p2p->ai_addr, result_p2p->ai_addrlen);
        freeaddrinfo(result_p2p);

        // extraction du nom de fichier
        char* nom_fichier = chemin_fichier_local; 
        for (int j = 0; chemin_fichier_local[j] != '\0'; j++) {
            if (chemin_fichier_local[j] == '/') nom_fichier = chemin_fichier_local + j + 1; 
        }

        // envoi par morceaux
        FILE* fichier_a_envoyer = fopen(chemin_fichier_local, "rb"); 
        if (fichier_a_envoyer != NULL) {
            char buffer_lecture[MSG_LEN]; 
            size_t octets_lus;
            
            while ((octets_lus = fread(buffer_lecture, 1, sizeof(buffer_lecture), fichier_a_envoyer)) > 0) {
                struct message msg_send;
                memset(&msg_send, 0, sizeof(struct message));
                msg_send.type = FILE_SEND;
                strncpy(msg_send.infos, nom_fichier, INFOS_LEN - 1);
                msg_send.pld_len = octets_lus;
                
                send(p2p_donnees_fd, &msg_send, sizeof(struct message), 0);
                send(p2p_donnees_fd, buffer_lecture, octets_lus, 0); 
            }
            
            // dernier paquet vide pour signaler la fin
            struct message msg_fin;
            memset(&msg_fin, 0, sizeof(struct message));
            msg_fin.type = FILE_SEND;
            msg_fin.pld_len = 0;
            send(p2p_donnees_fd, &msg_fin, sizeof(struct message), 0);
            fclose(fichier_a_envoyer);

            // attente de confirmation ack
            struct message msg_ack;
            lire_exactement(p2p_donnees_fd, &msg_ack, sizeof(struct message));
            if (msg_ack.type == FILE_ACK) {
                printf("%s has received the file.\n", pseudo_destinataire);
            }
        } else {
            printf("Erreur : impossible d'ouvrir le fichier '%s'.\n", chemin_fichier_local);
        }
        close(p2p_donnees_fd); 
    }
}



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
    char nom_fichier_attendu[MSG_LEN]; 
    memset(nom_fichier_attendu, 0, MSG_LEN);

    while (1) {
        int nbfds = poll(fds, 2, -1);
        if (nbfds < 0) break;

        // gestion clavier
        if (fds[0].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);
            int n = 0;
            while ((buff[n++] = getchar()) != '\n') { if (n >= MSG_LEN - 1) break; }
            buff[n] = '\0';
            
            if (strncmp("/quit", buff, 5) == 0) {
                printf("demande de deconnexion... \n"); break;
            }

            struct message msg; memset(&msg, 0, sizeof(struct message));
            strcpy(msg.nick_sender, my_pseudo);
            char* payload_a_envoyer = buff;

            if (en_attente_reponse_fichier && (buff[0] == 'Y' || buff[0] == 'y' || buff[0] == 'N' || buff[0] == 'n') && (buff[1] == '\0' || buff[1] == '\n')) {
                if (buff[0] == 'Y' || buff[0] == 'y') {
                    recevoir_fichier_p2p(sockfd, pseudo_emetteur_fichier, nom_fichier_attendu);
                } else {
                    msg.type = FILE_REJECT;
                    strncpy(msg.infos, pseudo_emetteur_fichier, INFOS_LEN - 1); 
                    msg.pld_len = 0;
                    send(sockfd, &msg, sizeof(struct message), 0);
                }
                en_attente_reponse_fichier = 0; 
                printf("Message: "); fflush(stdout);
                continue; 
            }
            else if (en_attente_reponse_fichier) {
                printf("Veuillez repondre par Y ou N.\n");
                fflush(stdout);
                continue;
            }

            else if (strncmp(buff, "/nick ", 6) == 0) {
                char* pseudo = buff + 6;
                pseudo[strcspn(pseudo, "\n")] = '\0';
                if (strlen(pseudo) >= NICK_LEN) { printf("Pseudo trop long.\nMessage: "); fflush(stdout); continue; }
                msg.type = NICKNAME_NEW; msg.pld_len = 0;
                strncpy(msg.infos, buff + 6, INFOS_LEN - 1); msg.infos[strcspn(msg.infos, "\n")] = 0;
            }
            else if (strcmp(buff, "/who\n") == 0) { msg.type = NICKNAME_LIST; msg.pld_len = 0; }
            else if (strncmp(buff, "/whois ", 7) == 0) {
                msg.type = NICKNAME_INFOS; msg.pld_len = 0;
                strncpy(msg.infos, buff + 7, INFOS_LEN - 1); msg.infos[strcspn(msg.infos, "\n")] = 0;
            }
            else if (strncmp(buff, "/msgall ", 8) == 0) {
                msg.type = BROADCAST_SEND; payload_a_envoyer = buff + 8; msg.pld_len = strlen(payload_a_envoyer);
            }
            else if (strncmp(buff, "/msg ", 5) == 0) {
                char* destinataire = buff + 5; char* message = NULL;
                for (int j = 0; destinataire[j] != '\0'; j++) {
                    if (destinataire[j] == ' ') { destinataire[j] = '\0'; message = destinataire + j + 1; break; }
                }
                if (message == NULL) { printf("Usage: /msg <pseudo> <message>\nMessage: "); fflush(stdout); continue; }
                msg.type = UNICAST_SEND; strncpy(msg.infos, destinataire, INFOS_LEN - 1);
                payload_a_envoyer = message; msg.pld_len = strlen(payload_a_envoyer);
            }
            else if (strncmp(buff, "/send ", 6) == 0) {
                char* destinataire = buff + 6; char* chemin_fichier = NULL;
                for (int j = 0; destinataire[j] != '\0'; j++) {
                    if (destinataire[j] == ' ') { destinataire[j] = '\0'; chemin_fichier = destinataire + j + 1; break; }
                }
                if (chemin_fichier == NULL) { printf("Usage: /send <pseudo> <chemin>\nMessage: "); fflush(stdout); continue; }
                if (chemin_fichier[0] == '"') {
                    chemin_fichier++; 
                    for (int j = 0; chemin_fichier[j] != '\0'; j++) { if (chemin_fichier[j] == '"') chemin_fichier[j] = '\0'; }
                }
                for (int j = 0; chemin_fichier[j] != '\0'; j++) { if (chemin_fichier[j] == '\n') { chemin_fichier[j] = '\0'; break; } }
                
                char* nom_fichier = chemin_fichier; 
                for (int j = 0; chemin_fichier[j] != '\0'; j++) { if (chemin_fichier[j] == '/') nom_fichier = chemin_fichier + j + 1; }

                strncpy(chemin_fichier_local, chemin_fichier, MSG_LEN - 1);
                msg.type = FILE_REQUEST; strncpy(msg.infos, destinataire, INFOS_LEN - 1); 
                payload_a_envoyer = nom_fichier; msg.pld_len = strlen(payload_a_envoyer);
            }
            else { msg.type = ECHO_SEND; msg.pld_len = strlen(buff); }

            if (send(sockfd, &msg, sizeof(struct message), 0) <= 0) break;
            if (msg.pld_len > 0) { if (send(sockfd, payload_a_envoyer, msg.pld_len, 0) <= 0) break; }

            if (msg.type == UNICAST_SEND || msg.type == BROADCAST_SEND || msg.type == FILE_REQUEST) {
                printf("Message: "); fflush(stdout);
            }
            fds[0].revents = 0;
        }

        // coté serveur
        if (fds[1].revents & POLLIN) {
            struct message reponse_msg;
            if (recv(sockfd, &reponse_msg, sizeof(struct message), 0) <= 0) { printf("\nServeur déconnecté.\n"); break; }

            char* payload = NULL;
            if (reponse_msg.pld_len > 0) {
                payload = malloc(reponse_msg.pld_len + 1);
                lire_exactement(sockfd, payload, reponse_msg.pld_len);
                payload[reponse_msg.pld_len] = '\0';

                if (reponse_msg.type == NICKNAME_NEW && strstr(payload, "Welcome on the chat")) {
                    char* espace = NULL;
                    for (int j = 0; payload[j] != '\0'; j++) { if (payload[j] == ' ') espace = payload + j; }
                    if (espace) { strncpy(my_pseudo, espace + 1, NICK_LEN - 1); my_pseudo[strcspn(my_pseudo, "\n")] = 0; }
                }
            }

            if (reponse_msg.type == FILE_REQUEST) {
                printf("\n%s wants you to accept the transfer of the file named \"%s\". Do you accept? [Y/N]\n", reponse_msg.nick_sender, payload);
                strncpy(pseudo_emetteur_fichier, reponse_msg.nick_sender, NICK_LEN - 1);
                strncpy(nom_fichier_attendu, payload, MSG_LEN - 1); 
                en_attente_reponse_fichier = 1;
            }
            else if (reponse_msg.type == FILE_ACCEPT) {
                printf("\n%s accepted file transfer.\n", reponse_msg.nick_sender);
                printf("Connecting to %s and sending the file...\n", reponse_msg.nick_sender);
                
                envoyer_fichier_p2p(payload, chemin_fichier_local, reponse_msg.nick_sender);
            }
            else if (reponse_msg.type == FILE_REJECT) { printf("\n%s cancelled file transfer.\n", reponse_msg.nick_sender); }
            else if (reponse_msg.type == UNICAST_SEND || reponse_msg.type == BROADCAST_SEND) { printf("\n[%s] : %s", reponse_msg.nick_sender, payload); }
            else { if (payload) printf("\n%s", payload); }

            if (payload) free(payload);
            if (!en_attente_reponse_fichier) { printf("Message: "); }
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