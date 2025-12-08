/*
 * protocol.h
 *
 * Server header file
 * Definitions, constants and function prototypes for the server
 */
#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#define BUFFER_SIZE 512
#define QUEUE_SIZE  5

#define DEFAULT_PORT 56700

#define CITY_MAX_LEN 64


#define STATUS_OK             0u
#define STATUS_CITY_NOT_FOUND 1u
#define STATUS_BAD_REQUEST    2u


#define TYPE_TEMP  't'
#define TYPE_HUM   'h'
#define TYPE_WIND  'w'
#define TYPE_PRESS 'p'


typedef struct {
    char type;                   // Weather data type: 't', 'h', 'w', 'p'
    char city[CITY_MAX_LEN];     // City name (null-terminated string)
} weather_request_t;

typedef struct {
    unsigned int status;         // 0,1,2
    char type;                   // eco del type, o '\0' se errore
    float value;                 // dato meteo
} weather_response_t;


float get_temperature(void);
float get_humidity(void);
float get_wind(void);
float get_pressure(void);

#endif /* PROTOCOL_H_ */
