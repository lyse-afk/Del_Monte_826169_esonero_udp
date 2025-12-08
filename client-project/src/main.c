/*
 * main.c
 *
 * UDP Client - Meteo
 */

#if defined WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#define closesocket close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "protocol.h"

void clearwinsock() {
#if defined WIN32
    WSACleanup();
#endif
}

static void print_usage(const char *progname) {
    printf("Uso: %s [-s server] [-p port] -r \"type city\"\n", progname);
}

int main(int argc, char *argv[]) {

    const char *server_name = "127.0.0.1";
    int port = DEFAULT_PORT;
    const char *req_str = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) server_name = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) req_str = argv[++i];
        else { print_usage(argv[0]); return 1; }
    }

    if (!req_str) { fprintf(stderr,"Parametro -r obbligatorio\n"); return 1; }

#if defined WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2,2), &wsa_data)!=0) { fprintf(stderr,"WSAStartup fallita\n"); return 1; }
#endif

    // Parsing richiesta
    if (req_str[0]=='\0' || req_str[1]!=' ') { fprintf(stderr,"Errore tipo richiesta\n"); return 1; }
    weather_request_t request;
    memset(&request,0,sizeof(request));
    request.type = req_str[0];

    const char *city_start = req_str + 1;
    while (*city_start==' ') city_start++;
    if (strlen(city_start) >= CITY_MAX_LEN) { fprintf(stderr,"Nome città troppo lungo\n"); return 1; }
    strncpy(request.city, city_start, CITY_MAX_LEN-1);

    // Risoluzione server
    struct addrinfo hints, *res;
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(server_name, NULL, &hints, &res)!=0) { perror("getaddrinfo"); return 1; }
    struct sockaddr_in server_addr = *(struct sockaddr_in*)res->ai_addr;
    server_addr.sin_port = htons((unsigned short)port);
    freeaddrinfo(res);

    // Creazione socket UDP
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock<0) { perror("socket"); clearwinsock(); return 1; }

    // Serializzazione request
    char buffer[1 + CITY_MAX_LEN];
    buffer[0] = request.type;
    memcpy(buffer+1, request.city, CITY_MAX_LEN);

    // Invio datagram
    if (sendto(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) != sizeof(buffer)) {
        perror("sendto");
        closesocket(sock); clearwinsock(); return 1;
    }

    // Ricezione risposta
    char rbuffer[sizeof(uint32_t)+1+sizeof(uint32_t)];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int bytes = recvfrom(sock, rbuffer, sizeof(rbuffer), 0, (struct sockaddr*)&from_addr, &from_len);
    if (bytes != sizeof(rbuffer)) { fprintf(stderr,"Risposta invalida\n"); closesocket(sock); clearwinsock(); return 1; }

    // Deserializzazione
    weather_response_t response;
    uint32_t tmp;

    memcpy(&tmp, rbuffer, sizeof(uint32_t));
    response.status = ntohl(tmp);
    response.type = rbuffer[4];
    memcpy(&tmp, rbuffer+5, sizeof(uint32_t));
    tmp = ntohl(tmp);
    memcpy(&response.value, &tmp, sizeof(float));

    // Risoluzione DNS reverse
    char host[NI_MAXHOST];
    if (getnameinfo((struct sockaddr*)&from_addr, from_len, host, sizeof(host), NULL, 0, 0)!=0) strcpy(host,"unknown");

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET,&from_addr.sin_addr,ip_str,sizeof(ip_str));

    // Stampa risultato
    printf("Ricevuto risultato dal server %s (ip %s). ", host, ip_str);
    if (response.status==STATUS_OK) {
        switch (response.type) {
            case TYPE_TEMP:  printf("%s: Temperatura = %.1f°C\n", request.city, response.value); break;
            case TYPE_HUM:   printf("%s: Umidità = %.1f%%\n", request.city, response.value); break;
            case TYPE_WIND:  printf("%s: Vento = %.1f km/h\n", request.city, response.value); break;
            case TYPE_PRESS: printf("%s: Pressione = %.1f hPa\n", request.city, response.value); break;
            default: printf("Risposta non valida\n"); break;
        }
    } else if (response.status==STATUS_CITY_NOT_FOUND) printf("Città non disponibile\n");
    else if (response.status==STATUS_BAD_REQUEST) printf("Richiesta non valida\n");
    else printf("Risposta non valida\n");

    closesocket(sock);
    clearwinsock();
    return 0;
}
