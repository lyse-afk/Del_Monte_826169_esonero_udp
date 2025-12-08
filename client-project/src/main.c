/*
 * main.c
 *
 * UDP Client - Portable for Windows/Linux/macOS
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
#include "protocol.h"
#include <stdint.h>

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

static void serialize_request(weather_request_t *req, char *buffer, int *len) {
    int offset = 0;
    buffer[offset++] = req->type;
    memcpy(buffer + offset, req->city, CITY_MAX_LEN);
    offset += CITY_MAX_LEN;
    *len = offset;
}

static void deserialize_response(weather_response_t *resp, char *buffer, int len) {
    int offset = 0;
    uint32_t net_status;
    memcpy(&net_status, buffer + offset, sizeof(uint32_t));
    resp->status = ntohl(net_status);
    offset += sizeof(uint32_t);

    memcpy(&resp->type, buffer + offset, sizeof(char));
    offset += sizeof(char);

    uint32_t net_value;
    memcpy(&net_value, buffer + offset, sizeof(uint32_t));
    net_value = ntohl(net_value);
    memcpy(&resp->value, &net_value, sizeof(float));
}

int main(int argc, char *argv[]) {
    const char *server_name = "127.0.0.1";
    int port = DEFAULT_PORT;
    const char *req_str = NULL;

    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i],"-s")==0 && i+1<argc) server_name = argv[++i];
        else if (strcmp(argv[i],"-p")==0 && i+1<argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i],"-r")==0 && i+1<argc) req_str = argv[++i];
        else { print_usage(argv[0]); return 1; }
    }

    if (!req_str) { fprintf(stderr,"Errore: parametro -r obbligatorio.\n"); print_usage(argv[0]); return 1; }

#if defined WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2,2), &wsa_data)!=0) { fprintf(stderr,"WSAStartup failed\n"); return 1; }
#endif

    // Parsing richiesta
    weather_request_t request;
    memset(&request,0,sizeof(request));
    if (strlen(req_str)<1) { fprintf(stderr,"Richiesta non valida\n"); return 1; }
    request.type = req_str[0];

    const char *city_start = req_str+1;
    while (*city_start==' ') city_start++;
    if (strlen(city_start)>=CITY_MAX_LEN) { fprintf(stderr,"Nome città troppo lungo\n"); return 1; }
    strncpy(request.city, city_start, CITY_MAX_LEN-1);
    request.city[CITY_MAX_LEN-1] = '\0';

    // Creazione socket UDP
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock<0) { perror("socket() failed"); clearwinsock(); return 1; }

    // Risoluzione server
    struct hostent *host = gethostbyname(server_name);
    if (!host) { fprintf(stderr,"gethostbyname() failed per %s\n", server_name); closesocket(sock); clearwinsock(); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);
    memcpy(&server_addr.sin_addr, host->h_addr_list[0], host->h_length);

    // Serializzazione richiesta
    char send_buffer[BUFFER_SIZE];
    int send_len = 0;
    serialize_request(&request, send_buffer, &send_len);

    if (sendto(sock, send_buffer, send_len, 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) != send_len) {
        fprintf(stderr,"sendto() failed\n"); closesocket(sock); clearwinsock(); return 1;
    }

    // Ricezione risposta
    char recv_buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(server_addr);
    int bytes = recvfrom(sock, recv_buffer, BUFFER_SIZE, 0, (struct sockaddr*)&server_addr, &addr_len);
    if (bytes <= 0) { fprintf(stderr,"recvfrom() failed\n"); closesocket(sock); clearwinsock(); return 1; }

    weather_response_t response;
    deserialize_response(&response, recv_buffer, bytes);

    char server_ip[64];
#if defined(__APPLE__) || defined(__linux__)
    inet_ntop(AF_INET, &server_addr.sin_addr, server_ip, sizeof(server_ip));
#else
    strcpy(server_ip, inet_ntoa(server_addr.sin_addr));
#endif

    printf("Ricevuto risultato dal server %s (ip %s). ", server_name, server_ip);

    if (response.status==STATUS_OK) {
        switch(response.type) {
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
