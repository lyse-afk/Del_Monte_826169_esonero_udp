/*
 * client.c
 *
 * UDP Client - Meteo
 */

#if defined WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#define closesocket close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "protocol.h"

void clearwinsock() {
#if defined WIN32
    WSACleanup();
#endif
}

static void print_usage(const char *progname) {
    printf("Uso: %s [-s server] [-p port] -r \"type city\"\n", progname);
    printf("  -s server   Indirizzo IP o hostname del server (default 127.0.0.1)\n");
    printf("  -p port     Porta del server (default %d)\n", DEFAULT_PORT);
    printf("  -r request  Richiesta meteo nel formato \"type city\" (obbligatoria)\n");
}

static int has_tab(const char *s) {
    while (*s) { if (*s == '\t') return 1; ++s; }
    return 0;
}

int main(int argc, char *argv[]) {
    const char *server_name = "127.0.0.1";
    int port = DEFAULT_PORT;
    const char *req_str = NULL;

    // parsing argomenti
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-s") == 0 && i+1 < argc) server_name = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i+1 < argc) req_str = argv[++i];
        else { print_usage(argv[0]); return 1; }
    }

    if (!req_str) { fprintf(stderr,"Errore: parametro -r obbligatorio.\n"); print_usage(argv[0]); return 1; }
    if (has_tab(req_str)) { fprintf(stderr,"Errore: caratteri di tabulazione non ammessi nella richiesta.\n"); return 1; }

#if defined WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2,2), &wsa_data) != 0) {
        fprintf(stderr,"WSAStartup failed\n"); return 1;
    }
#endif

    // parsing request
    char type = req_str[0];
    if (req_str[1] != ' ' && req_str[1] != '\0') {
        fprintf(stderr,"Errore: primo token deve essere un singolo carattere.\n"); clearwinsock(); return 1;
    }
    const char *city_start = req_str + 1;
    while (*city_start == ' ') city_start++;
    if (strlen(city_start) >= CITY_MAX_LEN) {
        fprintf(stderr,"Errore: nome città troppo lungo (max 63 caratteri)\n"); clearwinsock(); return 1;
    }

    // creazione socket UDP
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { perror("socket() failed"); clearwinsock(); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);

    struct hostent *host = gethostbyname(server_name);
    if (!host) { fprintf(stderr,"gethostbyname() fallita per %s\n", server_name); closesocket(sock); clearwinsock(); return 1; }
    memcpy(&server_addr.sin_addr, host->h_addr_list[0], host->h_length);

    // serializzazione richiesta
    char send_buf[1 + CITY_MAX_LEN];
    send_buf[0] = type;
    memset(send_buf+1,0,CITY_MAX_LEN);
    strncpy(send_buf+1, city_start, CITY_MAX_LEN-1);

    if (sendto(sock, send_buf, sizeof(send_buf), 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) != sizeof(send_buf)) {
        fprintf(stderr,"sendto() ha fallito\n"); closesocket(sock); clearwinsock(); return 1;
    }

    // ricezione risposta
    char recv_buf[sizeof(uint32_t)+1+sizeof(uint32_t)];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int n = recvfrom(sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr*)&from_addr, &from_len);
    if (n != sizeof(recv_buf)) { fprintf(stderr,"recvfrom() dimensione errata\n"); closesocket(sock); clearwinsock(); return 1; }

    weather_response_t response;
    int offset=0;
    uint32_t net_status;
    memcpy(&net_status, recv_buf+offset, sizeof(uint32_t));
    response.status = ntohl(net_status); offset+=sizeof(uint32_t);

    memcpy(&response.type, recv_buf+offset, 1); offset+=1;

    uint32_t net_val;
    memcpy(&net_val, recv_buf+offset, sizeof(uint32_t));
    net_val = ntohl(net_val);
    memcpy(&response.value, &net_val, sizeof(float));

    // risoluzione nome host server
    char server_ip_str[64]; char server_name_res[64];
#if defined(__APPLE__) || defined(__linux__)
    inet_ntop(AF_INET, &server_addr.sin_addr, server_ip_str, sizeof(server_ip_str));
#else
    strcpy(server_ip_str, inet_ntoa(server_addr.sin_addr));
#endif
    struct hostent *rev = gethostbyaddr(&server_addr.sin_addr, sizeof(server_addr.sin_addr), AF_INET);
    if (rev) strncpy(server_name_res, rev->h_name, sizeof(server_name_res)-1); else strncpy(server_name_res, server_name, sizeof(server_name_res)-1);
    server_name_res[sizeof(server_name_res)-1]='\0';

    printf("Ricevuto risultato dal server %s (ip %s). ", server_name_res, server_ip_str);
    switch(response.status) {
        case STATUS_OK:
            switch(response.type) {
                case TYPE_TEMP: printf("%s: Temperatura = %.1f°C\n", city_start, response.value); break;
                case TYPE_HUM:  printf("%s: Umidità = %.1f%%\n", city_start, response.value); break;
                case TYPE_WIND: printf("%s: Vento = %.1f km/h\n", city_start, response.value); break;
                case TYPE_PRESS:printf("%s: Pressione = %.1f hPa\n", city_start, response.value); break;
                default: printf("Risposta non valida\n"); break;
            }
            break;
        case STATUS_CITY_NOT_FOUND: printf("Città non disponibile\n"); break;
        case STATUS_BAD_REQUEST:    printf("Richiesta non valida\n"); break;
        default: printf("Risposta non valida\n"); break;
    }

    closesocket(sock);
    clearwinsock();
    return 0;
}
