#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

#define RB_SIZE 1024

typedef enum { ST_HEADER, ST_BODY } st_t;

typedef struct {
    char data[RB_SIZE];
    size_t read_pos;
    size_t write_pos;
    size_t used;
} ring_buffer_t;

typedef struct {
    st_t   state;
    size_t header_len;
    size_t content_length;
} http_parser_t;

typedef enum { PARSE_NEED_MORE, PARSE_DONE_ONE, PARSE_ERROR } parse_result_t;

static void ring_init(ring_buffer_t *rb){
    rb->read_pos = rb->write_pos = rb->used = 0;
}

static void send_simple_resonse(int client_fd){
   const char resp[] =
       "HTTP/1.1 200 OK\r\n"
       "Content-Length: 2\r\n"
       "Connection: close\r\n"
       "\r\n"
       "OK";
   (void)send(client_fd, resp, sizeof(resp) - 1, 0);
}

static int make_listen_socket(int port){
    int fd  = socket(AF_INET, SOCK_STREAM, 0);
    if(fd  < 0) {perror("socket"); return -1;}

    int opt = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        perror("bind");
        close(fd);
        return -1;
    }

    if(listen(fd, 128) < 0){
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

static ssize_t ring_write(ring_buffer_t *rb, const char *src, size_t len) {
    size_t written = 0;
    while (written < len && rb->used < RB_SIZE){
        rb->data[rb->write_pos] = src[written];
        rb->write_pos = (rb->write_pos + 1) % RB_SIZE;
        rb->used++;
        written++;
    }
    return (size_t)written;
}

static char ring_peek(const ring_buffer_t *rb, size_t i){
    return rb->data[(rb->read_pos + i) % RB_SIZE];
}

static void ring_consume(ring_buffer_t *rb, size_t n){
    rb->read_pos = (rb->read_pos + n) % RB_SIZE;
    rb->used -= n;
}

static size_t find_header_len(ring_buffer_t *rb){
    if (rb->used < 4) return -1;
    for (size_t i = 0; i + 3 < rb->used; i++) {
        if (ring_peek(rb, i)     == '\r' &&
            ring_peek(rb, i + 1) == '\n' &&
            ring_peek(rb, i + 2) == '\r' &&
            ring_peek(rb, i + 3) == '\n') {
            return (ssize_t)(i + 4);
        }
    }
    return -1;
}

static size_t parse_content_length(const ring_buffer_t *rb, size_t header_len){
    // skeniramo samo unutar header dela (0..header_len-1)
    // tražimo token "Content-Length:" na početku linije
    size_t i = 0;

    while (i < header_len) {
        // početak linije je i
        // nađi kraj linije (\r\n)
        size_t line_start = i;
        size_t line_end = i;

        while (line_end + 1 < header_len) {
            char c1 = ring_peek(rb, line_end);
            char c2 = ring_peek(rb, line_end + 1);
            if (c1 == '\r' && c2 == '\n') break;
            line_end++;
        }

        // prazna linija => kraj headera
        if (line_end == line_start) break;

        // uporedi prefiks u toj liniji
        const char key[] = "Content-Length:";
        size_t keylen = sizeof(key) - 1;

        if ((line_end - line_start) >= keylen) {
            // uporedi char-po-char (case-insensitive)
            int match = 1;
            for (size_t k = 0; k < keylen; k++) {
                char a = ring_peek(rb, line_start + k);
                char b = key[k];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b) { match = 0; break; }
            }

            if (match) {
                // preskoči key + whitespace
                size_t p = line_start + keylen;
                while (p < line_end) {
                    char c = ring_peek(rb, p);
                    if (c != ' ' && c != '\t') break;
                    p++;
                }

                // parse broj (kopiramo samo broj u mali buf)
                char numbuf[32];
                size_t nb = 0;
                while (p < line_end && nb + 1 < sizeof(numbuf)) {
                    char c = ring_peek(rb, p);
                    if (c < '0' || c > '9') break;
                    numbuf[nb++] = c;
                    p++;
                }
                numbuf[nb] = '\0';
                return (nb > 0) ? (size_t)strtoul(numbuf, NULL, 10) : 0;
            }
        }

        // pređi na sledeću liniju: line_end je na '\r', preskoči "\r\n"
        i = line_end + 2;
    }

    return 0;
}

static parse_result_t http_try_parse_one(ring_buffer_t *rb, http_parser_t *p){
    if (p->state == ST_HEADER) {
        ssize_t header_l = find_header_len(rb);
        if (header_l < 0) return PARSE_NEED_MORE;

        p->header_len = (size_t)header_l;
        p->content_length = parse_content_length(rb, p->header_len);

        ring_consume(rb, p->header_len);
        p->state = ST_BODY;
    }

    if (p->state == ST_BODY) {
        if (rb->used < p->content_length) return PARSE_NEED_MORE;

        ring_consume(rb, p->content_length);

        p->state = ST_HEADER;
        p->header_len = 0;
        p->content_length = 0;

        return PARSE_DONE_ONE;
    }

    return PARSE_ERROR;
}


int main(){

    int server_fd = make_listen_socket(8080);
    if(server_fd < 0) return 1;

    printf("Listening on 0.0.0.0:8080\n");

    for(;;){
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);

        int client_fd = accept(server_fd, (struct sockaddr*) &peer, &peer_len);

        ring_buffer_t rb;
        ring_init(&rb);

        http_parser_t parser = {0};
        parser.state = ST_HEADER;

        if(client_fd < 0){
            perror("accept");
            continue;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        printf("client: %s:%d\n", ip, ntohs(peer.sin_port));

        char buf[1024];

        for (;;) {
            ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
            if (n == 0) break;
            if (n < 0) { perror("recv"); break; }

            ssize_t written = ring_write(&rb, buf, (size_t)n);
            if (written < n) {
                fprintf(stderr, "ring buffer overflow\n");
                break;
            }

            // Parsiraj odmah nakon što si dodao nove bajtove
            for (;;) {
                parse_result_t r = http_try_parse_one(&rb, &parser);

                if (r == PARSE_NEED_MORE) break;
                if (r == PARSE_ERROR) { fprintf(stderr, "parse error\n"); goto done; }

                // got one complete request
                send_simple_resonse(client_fd);
                goto done; // za sada: jedna req -> jedna resp -> close
            }
        }

    done:
        close(client_fd);

    }
    close(server_fd);

    return 0;
}
