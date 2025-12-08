/*
 * server.c
 *
 * UDP Server - Meteo
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
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include "protocol.h"

void clearwinsock() {
#if defined WIN32
    WSACleanup();
#endif
}

static int equals_ignore_case(const char *a,const char *b) {
    if(!a||!b) return 0;
    while(*a && *b) { if(tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return 0; ++a;++b; }
    return *a=='\0' && *b=='\0';
}

static int is_supported_city(const char *city) {
    const char *cities[] = {"Bari","Roma","Milano","Napoli","Torino","Palermo","Genova","Bologna","Firenze","Venezia"};
    for(size_t i=0;i<sizeof(cities)/sizeof(cities[0]);++i) if(equals_ignore_case(city,cities[i])) return 1;
    return 0;
}

static float float_rand(float min, float max) { return min + ((float)rand()/RAND_MAX)*(max-min); }

float get_temperature(void) { return float_rand(-10.0f,40.0f); }
float get_humidity(void)    { return float_rand(20.0f,100.0f); }
float get_wind(void)        { return float_rand(0.0f,100.0f); }
float get_pressure(void)    { return float_rand(950.0f,1050.0f); }

int main(int argc,char*argv[]) {
    int port = DEFAULT_PORT;

#if defined WIN32
    WSADATA wsa_data;
    if(WSAStartup(MAKEWORD(2,2),&wsa_data)!=0) { fprintf(stderr,"WSAStartup failed\n"); return 1; }
#endif

    for(int i=1;i<argc;++i) { if(strcmp(argv[i],"-p")==0 && i+1<argc) port=atoi(argv[++i]); else { fprintf(stderr,"Uso: %s [-p port]\n",argv[0]); return 1; } }

    srand((unsigned int)time(NULL));

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock<0) { perror("socket() failed"); clearwinsock(); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(sock,(struct sockaddr*)&server_addr,sizeof(server_addr))<0) { perror("bind() failed"); closesocket(sock); clearwinsock(); return 1; }

    printf("Server in ascolto sulla porta %d\n", port);

    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char buf[1+CITY_MAX_LEN];

        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&client_addr, &client_len);
        if(n != sizeof(buf)) { fprintf(stderr,"recvfrom() dimensione errata\n"); continue; }

        weather_request_t request;
        request.type = buf[0];
        strncpy(request.city, buf+1, CITY_MAX_LEN-1); request.city[CITY_MAX_LEN-1]='\0';

        char client_ip[64]; char client_name[64];
#if defined(__APPLE__) || defined(__linux__)
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
#else
        strcpy(client_ip, inet_ntoa(client_addr.sin_addr));
#endif
        struct hostent *rev = gethostbyaddr(&client_addr.sin_addr,sizeof(client_addr.sin_addr),AF_INET);
        if(rev) strncpy(client_name,rev->h_name,sizeof(client_name)-1); else strncpy(client_name,client_ip,sizeof(client_name)-1);
        client_name[sizeof(client_name)-1]='\0';

        printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n", client_name, client_ip, request.type, request.city);

        weather_response_t response;
        memset(&response,0,sizeof(response));
        int type_ok = (request.type==TYPE_TEMP || request.type==TYPE_HUM || request.type==TYPE_WIND || request.type==TYPE_PRESS);
        int city_ok = is_supported_city(request.city);

        if(!type_ok) response.status = STATUS_BAD_REQUEST;
        else if(!city_ok) response.status = STATUS_CITY_NOT_FOUND;
        else { response.status=STATUS_OK; response.type=request.type;
            switch(request.type) {
                case TYPE_TEMP: response.value=get_temperature(); break;
                case TYPE_HUM:  response.value=get_humidity(); break;
                case TYPE_WIND: response.value=get_wind(); break;
                case TYPE_PRESS:response.value=get_pressure(); break;
            }
        }

        // serializzazione risposta
        char send_buf[sizeof(uint32_t)+1+sizeof(uint32_t)];
        int offset=0;
        uint32_t net_status=htonl(response.status);
        memcpy(send_buf+offset,&net_status,sizeof(uint32_t)); offset+=sizeof(uint32_t);
        memcpy(send_buf+offset,&response.type,1); offset+=1;
        uint32_t temp; memcpy(&temp,&response.value,sizeof(float)); temp=htonl(temp); memcpy(send_buf+offset,&temp,sizeof(uint32_t)); offset+=sizeof(uint32_t);

        if(sendto(sock,send_buf,offset,0,(struct sockaddr*)&client_addr,client_len)!=offset)
            fprintf(stderr,"sendto() fallita per client %s\n", client_ip);
    }

    closesocket(sock);
    clearwinsock();
    return 0;
}
