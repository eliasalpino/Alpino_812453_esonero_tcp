#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
#endif

#include "protocol.h"

int platform_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    return (WSAStartup(MAKEWORD(2,2), &wsa) == 0) ? 0 : -1;
#else
    (void)0;
    return 0;
#endif
}

void platform_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

uint32_t float_to_net(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    u = htonl(u);
    return u;
}

float net_to_float(uint32_t v) {
    uint32_t h = ntohl(v);
    float f;
    memcpy(&f, &h, sizeof(f));
    return f;
}

static int send_all(int sock, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    size_t left = len;
    while (left > 0) {
        int n = send(sock, p, (int)left, 0);
        if (n <= 0) return -1;
        left -= (size_t)n;
        p += n;
    }
    return 0;
}

static int recv_all(int sock, void* buf, size_t len) {
    char* p = (char*)buf;
    size_t left = len;
    while (left > 0) {
        int n = recv(sock, p, (int)left, 0);
        if (n <= 0) return -1;
        left -= (size_t)n;
        p += n;
    }
    return 0;
}

int send_request_and_receive_response(int sockfd,
                                      const weather_request_t* req,
                                      weather_response_t* resp) {
    if (send_all(sockfd, req, sizeof(*req)) != 0) return -1;
    if (recv_all(sockfd, resp, sizeof(*resp)) != 0) return -1;
    return 0;
}

void print_usage(const char* prog) {
    printf("Uso: %s [-s server] [-p port] -r \"type city\"\n", prog);
    printf("  -s server    Indirizzo server (default " DEFAULT_SERVER_IP ")\n");
    printf("  -p port      Porta server (default %d)\n", DEFAULT_SERVER_PORT);
    printf("  -r request   Richiesta nel formato \"type city\" (obbligatorio)\n");
    printf("               type: 't' 'h' 'w' 'p'\n");
}

int main(int argc, char* argv[]) {
    char server_ip[64] = DEFAULT_SERVER_IP;
    int port = DEFAULT_SERVER_PORT;
    char request_arg[BUFFER_SIZE] = {0};
    int have_request = 0;

    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i], "-s")==0 && i+1<argc) {
            strncpy(server_ip, argv[++i], sizeof(server_ip)-1);
        } else if (strcmp(argv[i], "-p")==0 && i+1<argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r")==0 && i+1<argc) {
            strncpy(request_arg, argv[++i], sizeof(request_arg)-1);
            have_request = 1;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!have_request) {
        print_usage(argv[0]);
        return 1;
    }

    char type = request_arg[0];
    char city[CITY_NAME_LEN];
    memset(city, 0, sizeof(city));

    if (strlen(request_arg) > 1) {
        const char* p = request_arg + 1;
        while (*p == ' ') p++;
        strncpy(city, p, CITY_NAME_LEN-1);
    } else {
        city[0] = '\0';
    }

    if (city[0] == '\0') {
        fprintf(stderr, "Errore: city vuota nella request\n");
        return 1;
    }

    if (!(type=='t' || type=='h' || type=='w' || type=='p')) {
    }

    if (platform_init() != 0) { fprintf(stderr, "Platform init failed\n"); return 1; }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); platform_cleanup(); return 1; }

    struct sockaddr_in srv;
    memset(&srv,0,sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);

    srv.sin_addr.s_addr = inet_addr(server_ip);
    if (srv.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "Indirizzo server non valido: %s\n", server_ip);
    #ifdef _WIN32
        closesocket(sock);
    #else
        close(sock);
    #endif
        platform_cleanup();
        return 1;
    }

    if (connect(sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("Errore connect");
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        platform_cleanup();
        return 1;
    }

    weather_request_t req;
    memset(&req, 0, sizeof(req));
    req.type = type;
    strncpy(req.city, city, CITY_NAME_LEN-1);
    req.city[CITY_NAME_LEN-1] = '\0';

    weather_response_t resp;
    if (send_request_and_receive_response(sock, &req, &resp) != 0) {
        fprintf(stderr, "Errore invio/ricezione\n");
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        platform_cleanup();
        return 1;
    }

    uint32_t status = ntohl(resp.status);

    uint32_t netf;
    memcpy(&netf, &resp.value, sizeof(netf));
    float value = net_to_float(netf);

    char output[256];
    if (status == STATUS_SUCCESS) {
        switch (resp.type) {
            case 't':
                snprintf(output, sizeof(output), "%s: Temperatura = %.1f°C", req.city, value);
                break;
            case 'h':
                snprintf(output, sizeof(output), "%s: Umidità = %.1f%%", req.city, value);
                break;
            case 'w':
                snprintf(output, sizeof(output), "%s: Vento = %.1f km/h", req.city, value);
                break;
            case 'p':
                snprintf(output, sizeof(output), "%s: Pressione = %.1f hPa", req.city, value);
                break;
            default:
                snprintf(output, sizeof(output), "Richiesta non valida");
                break;
        }
    } else if (status == STATUS_CITY_NOT_FOUND) {
        snprintf(output, sizeof(output), "Città non disponibile");
    } else if (status == STATUS_INVALID_REQUEST) {
        snprintf(output, sizeof(output), "Richiesta non valida");
    } else {
        snprintf(output, sizeof(output), "Risposta sconosciuta (status=%u)", status);
    }

    printf("Ricevuto risultato dal server ip %s. %s\n", server_ip, output);

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif

    platform_cleanup();
    return 0;
}
