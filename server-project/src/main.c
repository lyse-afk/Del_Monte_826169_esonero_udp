/*
 * main.c
 *
 * UDP Server - Meteo
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
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include "protocol.h"

void clearwinsock() {
#if defined WIN32
    WSACleanup();
#endif
}

// Funzioni "fake" per generare valori meteo
float get_temperature(void) { return 20.0f + (rand()%1000)/100.0f; } // 20.0-29.9 °C
float get_humidity(void)    { return 40.0f + (rand()%600)/10.0f; }   // 40.0-99.9 %
float get_wind(void)        { return (rand()%2000)/10.0f; }          // 0-199.9 km/h
float get_pressure(void)    { return 980.0f + (rand()%400)/10.0f; }  // 980-1019.9 hPa

static int equals_ignore_case(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a=='\0' && *b=='\0';
}

static int is_supported_city(const char *city) {
    const char *cities[] = {"Bari","Roma","Milano","Napoli","Torino","Palermo","Genova","Bologna","Firenze","Venezia"};
    for (size_t i=0;i<sizeof(cities)/sizeof(cities[0]);++i)
        if (equals_ignore_case(city,cities[i])) return 1;
    return 0;
}

int main(int argc,char *argv[]) {
    int port = DEFAULT_PORT;
#if defined WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2,2), &wsa_data)!=0) { fprintf(stderr,"WSAStartup failed\n"); return 1; }
#endif

    for (int i=1;i<argc;++i) {
        if (strcmp(argv[i],"-p")==0 && i+1<argc) port=atoi(argv[++i]);
    }

    srand((unsigned int)time(NULL));

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock<0) { perror("socket"); clearwinsock(); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock,(struct sockaddr*)&server_addr,sizeof(server_addr))<0) {
        perror("bind"); closesocket(sock); clearwinsock(); return 1;
    }

    printf("Server UDP in ascolto sulla porta %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char buffer[1+CITY_MAX_LEN];
        int bytes = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_len);
        if (bytes != sizeof(buffer)) { fprintf(stderr,"Datagramma invalido\n"); continue; }

        // Deserializzazione richiesta
        weather_request_t request;
        request.type = buffer[0];
        memcpy(request.city, buffer+1, CITY_MAX_LEN);

        // Log richiesta
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,&client_addr.sin_addr,client_ip,sizeof(client_ip));
        char client_host[NI_MAXHOST];
        if (getnameinfo((struct sockaddr*)&client_addr, client_len, client_host,sizeof(client_host), NULL,0,0)!=0)
            strcpy(client_host,"unknown");

        printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n", client_host, client_ip, request.type, request.city);

        // Validazione
        int type_ok = (request.type==TYPE_TEMP || request.type==TYPE_HUM || request.type==TYPE_WIND || request.type==TYPE_PRESS);
        int city_ok = is_supported_city(request.city);

        weather_response_t response;
        memset(&response,0,sizeof(response));
        if (!type_ok) response.status = STATUS_BAD_REQUEST;
        else if (!city_ok) response.status = STATUS_CITY_NOT_FOUND;
        else {
            response.status = STATUS_OK;
            response.type = request.type;
            switch (request.type) {
                case TYPE_TEMP:  response.value = get_temperature(); break;
                case TYPE_HUM:   response.value = get_humidity(); break;
                case TYPE_WIND:  response.value = get_wind(); break;
                case TYPE_PRESS: response.value = get_pressure(); break;
            }
        }

        // Serializzazione risposta
        char sbuffer[sizeof(uint32_t)+1+sizeof(uint32_t)];
        uint32_t tmp;
        tmp = htonl(response.status);
        memcpy(sbuffer,&tmp,sizeof(uint32_t));
        sbuffer[4] = response.type;
        memcpy(&tmp,&response.value,sizeof(float));
        tmp = htonl(tmp);
        memcpy(sbuffer+5,&tmp,sizeof(uint32_t));

        sendto(sock,sbuffer,sizeof(sbuffer),0,(struct sockaddr*)&client_addr,client_len);
    }

    closesocket(sock);
    clearwinsock();
    return 0;
}
