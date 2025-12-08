/*
 * UDP Client - Meteo
 */

#if defined WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>


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

// Convert float to network order
static void float_to_network(float f, uint32_t *out) {
    uint32_t temp;
    memcpy(&temp, &f, sizeof(float));
    *out = htonl(temp);
}

// Convert float from network order
static float network_to_float(uint32_t in) {
    uint32_t temp = ntohl(in);
    float f;
    memcpy(&f, &temp, sizeof(float));
    return f;
}

// Resolve server name to IP and canonical hostname
static int resolve_server(const char *name, struct sockaddr_in *addr, char *canon_name, size_t cname_len) {
    struct hostent *host = gethostbyname(name);
    if (!host) return 0;
    memcpy(&addr->sin_addr, host->h_addr_list[0], host->h_length);
    strncpy(canon_name, host->h_name, cname_len-1);
    canon_name[cname_len-1] = '\0';
    return 1;
}

int main(int argc, char *argv[]) {
    const char *server_name = "127.0.0.1";
    int port = DEFAULT_PORT;
    const char *req_str = NULL;

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"-s")==0 && i+1<argc) server_name = argv[++i];
        else if(strcmp(argv[i],"-p")==0 && i+1<argc) port = atoi(argv[++i]);
        else if(strcmp(argv[i],"-r")==0 && i+1<argc) req_str = argv[++i];
        else { print_usage(argv[0]); return 1; }
    }
    if(!req_str){ fprintf(stderr,"Errore: parametro -r obbligatorio.\n"); print_usage(argv[0]); return 1; }

#if defined WIN32
    WSADATA wsa_data;
    if(WSAStartup(MAKEWORD(2,2),&wsa_data)!=0){ fprintf(stderr,"WSAStartup failed\n"); return 1; }
#endif

    // Parse request
    if(req_str[0]=='\0' || req_str[1]!=' '){ fprintf(stderr,"Richiesta non valida\n"); return 1; }

    weather_request_t request;
    memset(&request,0,sizeof(request));
    request.type = req_str[0];

    const char *city_start = req_str+1;
    while(*city_start==' ') city_start++;
    if(strlen(city_start) >= CITY_MAX_LEN) { fprintf(stderr,"Nome città troppo lungo\n"); return 1; }
    if(strchr(city_start,'\t')) { fprintf(stderr,"Richiesta non valida\n"); return 1; }
    strncpy(request.city, city_start, CITY_MAX_LEN-1);
    request.city[CITY_MAX_LEN-1] = '\0';

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock<0){ perror("socket() failed"); clearwinsock(); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);

    char canon_name[256];
    if(!resolve_server(server_name,&server_addr,canon_name,sizeof(canon_name))){
        fprintf(stderr,"Risoluzione server fallita\n"); closesocket(sock); clearwinsock(); return 1; }

    // Serialize request
    char buffer[1 + CITY_MAX_LEN];
    buffer[0] = request.type;
    memcpy(buffer+1, request.city, CITY_MAX_LEN);

    if(sendto(sock,buffer,sizeof(buffer),0,(struct sockaddr*)&server_addr,sizeof(server_addr)) != sizeof(buffer)){
        fprintf(stderr,"Invio richiesta fallito\n"); closesocket(sock); clearwinsock(); return 1; }

    // Receive response
    weather_response_t response;
    char resp_buffer[sizeof(uint32_t)+1+sizeof(uint32_t)];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int n = recvfrom(sock,resp_buffer,sizeof(resp_buffer),0,(struct sockaddr*)&from_addr,&from_len);
    if(n != sizeof(resp_buffer)){ fprintf(stderr,"Risposta non valida\n"); closesocket(sock); clearwinsock(); return 1; }

    int offset=0;
    uint32_t net_status, net_value;
    memcpy(&net_status,resp_buffer+offset,sizeof(uint32_t)); offset+=sizeof(uint32_t);
    response.status = ntohl(net_status);
    memcpy(&response.type,resp_buffer+offset,1); offset+=1;
    memcpy(&net_value,resp_buffer+offset,sizeof(uint32_t));
    response.value = network_to_float(net_value);

    char ip_str[64];
#if defined(__linux__) || defined(__APPLE__)
    inet_ntop(AF_INET,&server_addr.sin_addr,ip_str,sizeof(ip_str));
#else
    strcpy(ip_str, inet_ntoa(server_addr.sin_addr));
#endif

    printf("Ricevuto risultato dal server %s (ip %s). ", canon_name, ip_str);
    switch(response.status){
        case STATUS_OK:
            switch(response.type){
                case TYPE_TEMP:  printf("%s: Temperatura = %.1f°C\n", request.city, response.value); break;
                case TYPE_HUM:   printf("%s: Umidità = %.1f%%\n", request.city, response.value); break;
                case TYPE_WIND:  printf("%s: Vento = %.1f km/h\n", request.city, response.value); break;
                case TYPE_PRESS: printf("%s: Pressione = %.1f hPa\n", request.city, response.value); break;
            } break;
        case STATUS_CITY_NOT_FOUND: printf("Città non disponibile\n"); break;
        case STATUS_BAD_REQUEST:    printf("Richiesta non valida\n"); break;
        default: printf("Risposta non valida\n"); break;
    }

    closesocket(sock);
    clearwinsock();
    return 0;
}
