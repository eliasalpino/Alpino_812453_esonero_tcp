#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
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

static float rand_range(float a, float b) {
    return a + (float)rand() / (float)RAND_MAX * (b - a);
}

float get_temperature(void) { return rand_range(-10.0f, 40.0f); }
float get_humidity(void)    { return rand_range(20.0f, 100.0f); }
float get_wind(void)        { return rand_range(0.0f, 100.0f); }
float get_pressure(void)    { return rand_range(950.0f, 1050.0f); }

static const char* supported_cities[] = {
    "Bari","Roma","Milano","Napoli","Torino","Palermo",
    "Genova","Bologna","Firenze","Venezia"
};
static const int supported_count = sizeof(supported_cities)/sizeof(supported_cities[0]);

int city_supported(const char* city) {
    for (int i=0;i<supported_count;i++) {
        if (STRCASECMP(city, supported_cities[i]) == 0)
            return 1;
    }
    return 0;
}

static int send_all(int sock, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    size_t left = len;
    while (left > 0) {
        int n = send(sock, p, (int)left, 0);
        if (n <= 0) return -1;
        left -= n;
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
        left -= n;
        p += n;
    }
    return 0;
}

void handle_one_client(int client_sock, const char* client_ip) {
    weather_request_t req;
    weather_response_t resp;

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    if (recv_all(client_sock, &req, sizeof(req)) != 0) {
        fprintf(stderr, "[SERVER] Errore ricezione richiesta da %s\n", client_ip);
        return;
    }

    printf("Richiesta '%c %s' dal client %s\n",
           req.type, req.city, client_ip);

    if (!(req.type=='t' || req.type=='h' ||
          req.type=='w' || req.type=='p')) {
        resp.status = htonl(STATUS_INVALID_REQUEST);
        resp.type = 0;
        resp.value = 0;
        send_all(client_sock, &resp, sizeof(resp));
        return;
    }

    if (!city_supported(req.city)) {
        resp.status = htonl(STATUS_CITY_NOT_FOUND);
        resp.type = 0;
        resp.value = 0;
        send_all(client_sock, &resp, sizeof(resp));
        return;
    }

    float val = 0;
    switch(req.type) {
        case 't': val = get_temperature(); break;
        case 'h': val = get_humidity();    break;
        case 'w': val = get_wind();        break;
        case 'p': val = get_pressure();    break;
    }

    resp.status = htonl(STATUS_SUCCESS);
    resp.type = req.type;

    uint32_t netf = float_to_net(val);
    memcpy(&resp.value, &netf, sizeof(netf));

    send_all(client_sock, &resp, sizeof(resp));
}

int main(int argc, char** argv) {
    int port = DEFAULT_SERVER_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i+1 < argc) {
            port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Uso: %s [-p port]\n", argv[0]);
            return 1;
        }
    }

    if (platform_init() != 0) {
        fprintf(stderr, "Errore: platform_init fallita\n");
        return 1;
    }

    srand((unsigned)time(NULL));

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        perror("socket");
        platform_cleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (char*)&opt, sizeof(opt));

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port = htons(port);

    if (bind(listen_sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("bind");
        platform_cleanup();
        return 1;
    }

    if (listen(listen_sock, 5) < 0) {
        perror("listen");
        platform_cleanup();
        return 1;
    }

    printf("[SERVER] Meteo attivo sulla porta %d\n", port);
    printf("[SERVER] In attesa di connessioni...\n");

    while (1) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);

        int client_sock = accept(listen_sock, (struct sockaddr*)&cli, &len);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        const char* ip = inet_ntoa(cli.sin_addr);
        printf("[SERVER] Client connesso da %s\n", ip);

        handle_one_client(client_sock, ip);

    #ifdef _WIN32
        closesocket(client_sock);
    #else
        close(client_sock);
    #endif

        printf("[SERVER] In attesa di connessioni...\n");
    };

    platform_cleanup();
    return 0;
}
