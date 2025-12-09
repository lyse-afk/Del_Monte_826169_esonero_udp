/*
 * main.c
 *
 * UDP Server - Template for Computer Networks assignment
 *
 * This file contains the boilerplate code for a UDP server
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
#include <ctype.h>
#include <time.h>
#include "protocol.h"

#ifndef NO_ERROR
#define NO_ERROR 0
#endif

#define BUFFER_SIZE 512

void clearwinsock() {
#if defined WIN32 || defined _WIN32
	WSACleanup();
#endif
}

// Helper functions logic
int is_valid_city(const char *city) {
	const char* available_cities[] = {"bari", "roma", "milano", "napoli", "torino", "palermo", "genova", "bologna", "firenze", "venezia"};
	int n = 10;
	for (int i = 0; i < n; i++) {
#if defined WIN32 || defined _WIN32
		if (_stricmp(city, available_cities[i]) == 0) return 1;
#else
		if (strcasecmp(city, available_cities[i]) == 0) return 1;
#endif
	}
	return 0;
}

int is_valid_type(char type) {
	char l = tolower(type);
	return (l == 't' || l == 'h' || l == 'w' || l == 'p');
}


int has_invalid_chars(const char *city) {
	for (int i = 0; city[i] != '\0'; i++) {
		// Se il carattere NON è una lettera E NON è uno spazio  è invalido
		if (!isalnum(city[i]) && city[i] != ' ') {
			return 1;
		}
	}
	return 0;
}

float get_temperature(void) { return -10.0 + (rand() / (float)RAND_MAX) * 50.0; }
float get_humidity(void) { return 20.0 + (rand() / (float)RAND_MAX) * 80.0; }
float get_wind(void) { return (rand() / (float)RAND_MAX) * 100.0; }
float get_pressure(void) { return 950.0 + (rand() / (float)RAND_MAX) * 100.0; }

int main(int argc, char *argv[]) {

	// Implement server logic
	srand(time(NULL));
	int port = SERVER_PORT;

	if (argc == 3 && strcmp(argv[1], "-p") == 0) {
		port = atoi(argv[2]);
	} else if (argc != 1) {
		printf("Uso: %s [-p port]\n", argv[0]);
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
	sad.sin_addr.s_addr = INADDR_ANY;
	sad.sin_port = htons(port);

	// Bind socket
	if (bind(my_socket, (struct sockaddr*)&sad, sizeof(sad)) < 0) {
		printf("Errore bind.\n");
		closesocket(my_socket);
		clearwinsock();
		return 1;
	}

	printf("Server UDP in ascolto sulla porta %d\n", port);

	// Implement UDP datagram reception loop
	char buffer[BUFFER_SIZE];
	struct sockaddr_in client_addr;
	int client_len;

	while (1) {
		client_len = sizeof(client_addr);
		int bytes_rcvd = recvfrom(my_socket, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);

		if (bytes_rcvd < 0) continue;

		// Log request
		struct hostent *he = gethostbyaddr((char *)&client_addr.sin_addr, 4, AF_INET);
		char *client_name = (he != NULL) ? he->h_name : inet_ntoa(client_addr.sin_addr);

		if (strcmp(inet_ntoa(client_addr.sin_addr), "127.0.0.1") == 0) {
			client_name = "localhost";
		}

		// Deserializzazione
		char req_type = buffer[0];
		char req_city[64];
		strncpy(req_city, buffer + 1, 63);
		req_city[63] = '\0';

		printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n", client_name, inet_ntoa(client_addr.sin_addr), req_type, req_city);

		// Logic
		unsigned int status = STATUS_OK;
		float value = 0.0;
		char type_lower = tolower(req_type);

		if (!is_valid_type(type_lower)) {

			status = STATUS_INVALID_REQUEST;
		}
		else if (has_invalid_chars(req_city)) {

			status = STATUS_INVALID_REQUEST;
			type_lower = req_type;
		}
		else if (!is_valid_city(req_city)) {

			status = STATUS_CITY_NOT_FOUND;
			type_lower = req_type;
		}
		else {

			switch (type_lower) {
				case 't': value = get_temperature(); break;
				case 'h': value = get_humidity(); break;
				case 'w': value = get_wind(); break;
				case 'p': value = get_pressure(); break;
			}
		}

		// Serializzazione response
		char send_buf[BUFFER_SIZE];
		int offset = 0;

		unsigned long net_status = htonl(status);
		memcpy(send_buf + offset, &net_status, sizeof(unsigned long));
		offset += sizeof(unsigned long);

		send_buf[offset] = (status == STATUS_INVALID_REQUEST) ? req_type : type_lower;
		offset += sizeof(char);

		unsigned long net_val;
		memcpy(&net_val, &value, sizeof(float));
		net_val = htonl(net_val);
		memcpy(send_buf + offset, &net_val, sizeof(unsigned long));
		offset += sizeof(unsigned long);

		sendto(my_socket, send_buf, offset, 0, (struct sockaddr*)&client_addr, client_len);
	}

	printf("Server terminated.\n");

	closesocket(my_socket);
	clearwinsock();
	return 0;
}
