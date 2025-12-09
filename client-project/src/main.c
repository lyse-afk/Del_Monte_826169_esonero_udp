/*
 * main.c
 *
 * UDP Client - Template for Computer Networks assignment
 *
 * This file contains the boilerplate code for a UDP client
 * portable across Windows, Linux, and macOS.
 */

#if defined WIN32 || defined _WIN32
#include <winsock.h>
typedef int socklen_t;
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
#include <string.h>
#include "protocol.h"

#define BUFFER_SIZE 512

void clearwinsock() {
#if defined WIN32 || defined _WIN32
	WSACleanup();
#endif
}

int main(int argc, char *argv[]) {

	// Implement client logic
	char *server_ip = "localhost";
	int port = SERVER_PORT;
	char *request_str = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			server_ip = argv[i + 1];
			i++;
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			port = atoi(argv[i + 1]);
			i++;
		} else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
			request_str = argv[i + 1];
			i++;
		} else {
			printf("Uso: %s [-s server] [-p port] -r \"tipo citta\"\n", argv[0]);
			return 1;
		}
	}

	if (request_str == NULL) {
		printf("Errore: parametro -r obbligatorio\n");
		return 1;
	}

	if (strchr(request_str, '\t') != NULL) {
		printf("Errore: la richiesta contiene caratteri di tabulazione non ammessi.\n");
		return 1;
	}
	char *first_space = strchr(request_str, ' ');
	if (first_space == NULL) {
		printf("Errore: formato richiesta errato. Esempio: \"t Roma\"\n");
		return 1;
	}

	int type_len = first_space - request_str;
	if (type_len != 1) {
		printf("Errore: il tipo richiesta deve essere un singolo carattere.\n");
		return 1;
	}

	char type = request_str[0];
	char *city_ptr = first_space + 1;

	if (strlen(city_ptr) >= 64) {
		printf("Errore: nome città troppo lungo.\n");
		return 1;
	}

#if defined WIN32 || defined _WIN32
	// Initialize Winsock
	WSADATA wsa_data;
	int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if (result != 0) {
		printf("Error at WSAStartup()\n");
		return 0;
	}
#endif

	int my_socket;

	// Create UDP socket
	my_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (my_socket < 0) {
		printf("Errore creazione socket.\n");
		clearwinsock();
		return 1;
	}

	// Configure server address
	struct sockaddr_in sad;
	memset(&sad, 0, sizeof(sad));
	sad.sin_family = AF_INET;
	sad.sin_port = htons(port);

	struct hostent *host = gethostbyname(server_ip);
	if (host == NULL) {
		printf("Errore risoluzione nome server.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}
	memcpy(&sad.sin_addr, host->h_addr_list[0], host->h_length);

	// Implement UDP communication logic
	char send_buf[BUFFER_SIZE];
	int offset = 0;

	// Serializzazione request
	send_buf[offset] = type;
	offset += sizeof(char);
	strcpy(send_buf + offset, city_ptr);
	offset += 64;

	if (sendto(my_socket, send_buf, offset, 0, (struct sockaddr*)&sad, sizeof(sad)) < 0) {
		printf("Errore sendto.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}

	char recv_buf[BUFFER_SIZE];
	struct sockaddr_in from_addr;
	int from_len = sizeof(from_addr);

	int bytes_rcvd = recvfrom(my_socket, recv_buf, BUFFER_SIZE, 0, (struct sockaddr*)&from_addr, (socklen_t*)&from_len);

	if (bytes_rcvd < 0) {
		printf("Errore recvfrom.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}

	// Deserializzazione
	unsigned int resp_status;
	char resp_type;
	float resp_value;
	int recv_offset = 0;

	unsigned long net_status;
	memcpy(&net_status, recv_buf + recv_offset, sizeof(unsigned long));
	resp_status = ntohl(net_status);
	recv_offset += sizeof(unsigned long);

	memcpy(&resp_type, recv_buf + recv_offset, sizeof(char));
	recv_offset += sizeof(char);

	unsigned long net_val;
	memcpy(&net_val, recv_buf + recv_offset, sizeof(unsigned long));
	net_val = ntohl(net_val);
	memcpy(&resp_value, &net_val, sizeof(float));

	// Reverse DNS output
	struct hostent *he = gethostbyaddr((char *)&from_addr.sin_addr, 4, AF_INET);
	char *server_name = (he != NULL) ? he->h_name : inet_ntoa(from_addr.sin_addr);
	char *server_addr_ip = inet_ntoa(from_addr.sin_addr);

	if (resp_status == STATUS_OK) {
		printf("Ricevuto risultato dal server %s (ip %s). %s: ", server_name, server_addr_ip, city_ptr);
		switch (resp_type) {
			case 't': printf("Temperatura = %.1f°C\n", resp_value); break;
			case 'h': printf("Umidità = %.1f%%\n", resp_value); break;
			case 'w': printf("Vento = %.1f km/h\n", resp_value); break;
			case 'p': printf("Pressione = %.1f hPa\n", resp_value); break;
		}
	} else if (resp_status == STATUS_CITY_NOT_FOUND) {
		printf("Ricevuto risultato dal server %s (ip %s). Città non disponibile\n", server_name, server_addr_ip);
	} else if (resp_status == STATUS_INVALID_REQUEST) {
		printf("Ricevuto risultato dal server %s (ip %s). Richiesta non valida\n", server_name, server_addr_ip);
	} else {
		printf("Ricevuto risultato dal server %s (ip %s). Errore sconosciuto\n", server_name, server_addr_ip);
	}

	// Close socket
	closesocket(my_socket);

	printf("Client terminated.\n");

	clearwinsock();
	return 0;
}
