/*
 * UDP Server - Meteo
 */

#if defined WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdint.h>

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include <time.h>
#define closesocket close


#include "protocol.h"

void clearwinsock() {
#if defined WIN32
    WSACleanup();
#endif
}

// Convert float to network order
static void float_to_network(float f, uint32_t *out){
    uint32_t temp;
    memcpy(&temp,&f,sizeof(float));
    *out = htonl(temp);
}

static int equals_ignore_case(const char *a, const char *b){
    while(*a && *b){
        if(tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a=='\0' && *b=='\0';
}

static int is_supported_city(const char *city){
    const char *cities[] = {"Bari","Roma","Milano","Napoli","Torino","Palermo","Genova","Bologna","Firenze","Venezia"};
    for(size_t i=0;i<sizeof(cities)/sizeof(cities[0]);i++){
        if(equals_ignore_case(city,cities[i])) return 1;
    }
    return 0;
}

static int city_has_invalid_chars(const char *city){
    while(*city){
        unsigned char c = (unsigned char)*city;
        if(c=='\t'||c=='@'||c=='#'||c=='$'||c=='%') return 1;
        city++;
    }
    return 0;
}

float get_temperature(void){ return -10.0f + ((float)rand()/RAND_MAX)*50.0f; }
float get_humidity(void){ return 20.0f + ((float)rand()/RAND_MAX)*80.0f; }
float get_wind(void){ return 0.0f + ((float)rand()/RAND_MAX)*100.0f; }
float get_pressure(void){ return 950.0f + ((float)rand()/RAND_MAX)*100.0f; }

int main(int argc,char *argv[]){
    int port = DEFAULT_PORT;

#if defined WIN32
    WSADATA wsa_data;
    if(WSAStartup(MAKEWORD(2,2),&wsa_data)!=0){ fprintf(stderr,"WSAStartup failed\n"); return 1; }
#endif

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"-p")==0 && i+1<argc){ port=atoi(argv[++i]);
            if(port<=0 || port>65535){ fprintf(stderr,"Invalid port.\n"); return 1; } }
        else { fprintf(stderr,"Usage: %s [-p port]\n",argv[0]); return 1; }
    }

    srand((unsigned int)time(NULL));

    int sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(sock<0){ perror("socket() failed"); clearwinsock(); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(sock,(struct sockaddr*)&server_addr,sizeof(server_addr))<0){ perror("bind() failed"); closesocket(sock); clearwinsock(); return 1; }

    printf("Server UDP in ascolto sulla porta %d\n",port);

    while(1){
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char buffer[1+CITY_MAX_LEN];
        int n = recvfrom(sock,buffer,sizeof(buffer),0,(struct sockaddr*)&client_addr,&client_len);
        if(n != sizeof(buffer)){ fprintf(stderr,"recvfrom() error\n"); continue; }

        weather_request_t request;
        request.type = buffer[0];
        memcpy(request.city,buffer+1,CITY_MAX_LEN);

        char client_ip[64];
#if defined(__linux__) || defined(__APPLE__)
        inet_ntop(AF_INET,&client_addr.sin_addr,client_ip,sizeof(client_ip));
#else
        strcpy(client_ip, inet_ntoa(client_addr.sin_addr));
#endif

        struct hostent *h = gethostbyaddr(&client_addr.sin_addr,sizeof(client_addr.sin_addr),AF_INET);
        char client_host[256];
        if(h) strncpy(client_host,h->h_name,sizeof(client_host)-1);
        else strncpy(client_host,client_ip,sizeof(client_host)-1);
        client_host[sizeof(client_host)-1]='\0';

        printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n", client_host, client_ip, request.type, request.city);

        weather_response_t response;
        memset(&response,0,sizeof(response));

        int type_ok = (request.type==TYPE_TEMP || request.type==TYPE_HUM || request.type==TYPE_WIND || request.type==TYPE_PRESS);
        int city_ok = is_supported_city(request.city);
        int city_invalid = city_has_invalid_chars(request.city);

        if(!type_ok || city_invalid) response.status = STATUS_BAD_REQUEST;
        else if(!city_ok) response.status = STATUS_CITY_NOT_FOUND;
        else {
            response.status = STATUS_OK;
            response.type = request.type;
            switch(request.type){
                case TYPE_TEMP:  response.value = get_temperature(); break;
                case TYPE_HUM:   response.value = get_humidity(); break;
                case TYPE_WIND:  response.value = get_wind(); break;
                case TYPE_PRESS: response.value = get_pressure(); break;
            }
        }

        // Serialize response
        char resp_buffer[sizeof(uint32_t)+1+sizeof(uint32_t)];
        int offset=0;
        uint32_t net_status = htonl(response.status);
        memcpy(resp_buffer+offset,&net_status,sizeof(uint32_t)); offset+=sizeof(uint32_t);
        memcpy(resp_buffer+offset,&response.type,1); offset+=1;
        uint32_t net_value;
        float_to_network(response.value,&net_value);
        memcpy(resp_buffer+offset,&net_value,sizeof(uint32_t));

        sendto(sock,resp_buffer,sizeof(resp_buffer),0,(struct sockaddr*)&client_addr,client_len);
    }

    closesocket(sock);
    clearwinsock();
    return 0;
}
