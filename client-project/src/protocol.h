#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h> 
    typedef int socklen_t;
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netinet/in.h>
#endif

#define DEFAULT_SERVER_IP   "127.0.0.1"
#define DEFAULT_SERVER_PORT 56700
#define BUFFER_SIZE         512

#define CITY_NAME_LEN 64

typedef struct {
    char type;
    char city[CITY_NAME_LEN];
} weather_request_t;

typedef struct {
    uint32_t status;
    char type;
    float value;
} weather_response_t;

#define STATUS_SUCCESS        0u
#define STATUS_CITY_NOT_FOUND 1u
#define STATUS_INVALID_REQUEST 2u

int platform_init(void);
void platform_cleanup(void);

float get_temperature(void);
float get_humidity(void);
float get_wind(void);
float get_pressure(void);

uint32_t float_to_net(float f);
float net_to_float(uint32_t v);

int send_request_and_receive_response(int sockfd,
                                      const weather_request_t* req,
                                      weather_response_t* resp);

#ifdef _WIN32
    #define STRCASECMP _stricmp
#else
    #define STRCASECMP strcasecmp
#endif

#endif // PROTOCOL_H
