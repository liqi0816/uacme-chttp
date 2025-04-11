#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void throw(const char *message, ...)
{
    va_list argv;
    va_start(argv, message);
    vfprintf(stderr, message, argv);
    exit(1);
}

int ensure_syscall(const char *message, const int ret)
{
    if (ret >= 0)
        return ret;
    perror(message);
    exit(2);
}

int atoi_default(const char *message, const int ret)
{
    return message ? (atoi(message) ?: ret) : ret;
}

const char *string_default(const char *message, const char *ret)
{
    return message ? (*message ? message : ret) : ret;
}

int find_r_in_chunk(const char *request_buf, const int request_buf_len, const int request_chunk_len)
{
    for (int i = request_buf_len; i < request_buf_len + request_chunk_len; i++)
        if (request_buf[i] == '\r')
            return i;
    return -1;
}

int main(const int argc, char *const *argv)
{
    if (argc > 1)
    {
        if (argc < 6)
            throw("%s begin http-01 IDENT TOKEN AUTH\n", argv[0]);
        if (strcmp(argv[2], "http-01") != 0)
            throw("<7>decline %s challenge\n", argv[2]);
        if (strcmp(argv[1], "begin") != 0)
            return 0;
    }
    const struct timeval server_timeout = {.tv_sec = atoi_default(getenv("UACME_CHTTP_TIMEOUT"), 10)};
    const char *const server_device = getenv("UACME_CHTTP_BINDTODEVICE") ?: "";
    struct addrinfo *server_addr;
    const int server_addr_err = getaddrinfo(getenv("UACME_CHTTP_ADDR") ?: "::", getenv("UACME_CHTTP_PORT") ?: "44380",
                                            &(const struct addrinfo){.ai_flags = AI_PASSIVE | AI_V4MAPPED | AI_ADDRCONFIG, .ai_socktype = SOCK_STREAM}, &server_addr);
    if (server_addr_err != 0)
        throw("<2>getaddrinfo: %s\n", gai_strerror(server_addr_err));
    const int server_fd = ensure_syscall("<2>socket", socket(server_addr->ai_family, server_addr->ai_socktype, 0));
    ensure_syscall("<2>setsockopt", setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(const int){1}, sizeof(int)));
    ensure_syscall("<2>setsockopt", setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &server_timeout, sizeof(server_timeout)));
    ensure_syscall("<2>setsockopt", setsockopt(server_fd, SOL_SOCKET, SO_BINDTODEVICE, server_device, strlen(server_device) + 1));
    ensure_syscall("<2>bind", bind(server_fd, server_addr->ai_addr, server_addr->ai_addrlen));
    ensure_syscall("<2>listen", listen(server_fd, 8));
    if ((argc > 1 || getenv("UACME_CHTTP_FORCE_DETACH")) && fork() != 0)
        return fprintf(stderr, "<4>%s(%d) detached from foreground\n", argv[0], getpid()), 0;

    for (int client_success_count = 0;; client_success_count++)
    {
        fprintf(stderr, "<6>wait %ld seconds for next client connection\n", server_timeout.tv_sec);
        struct sockaddr_storage client_addr;
        const int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &(socklen_t){sizeof(client_addr)});
        if (client_fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (client_success_count)
            {
                fprintf(stderr, "<4>completed %d clients\n", client_success_count);
                break;
            }
            else
                throw("<3>time out\n");
        }
        ensure_syscall("<2>accept", client_fd);
        ensure_syscall("<2>setsockopt", setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &(const struct timeval){.tv_sec = 1}, sizeof(struct timeval)));
        char client_ip[1024];
        char client_port[16];
        getnameinfo((const struct sockaddr *)&client_addr, sizeof(client_addr), client_ip, sizeof(client_ip), client_port, sizeof(client_port), NI_NUMERICHOST | NI_NUMERICSERV);
        fprintf(stderr, "<6>client connection from %s:%s\n", client_ip, client_port);

        char request_buf[1024];
        for (int request_buf_len = 0;;)
        {
            const int request_chunk_len = ensure_syscall("<2>recv", recv(client_fd, request_buf + request_buf_len, sizeof(request_buf) - request_buf_len, 0));
            if (request_chunk_len == 0)
                throw("<3>HTTP request line boundary not found\n");
            const int i = find_r_in_chunk(request_buf, request_buf_len, request_chunk_len);
            if (i > 0)
            {
                request_buf[i] = 0;
                break;
            }
            request_buf_len += request_chunk_len;
        }

        const char request_line_prefix[] = "GET /.well-known/acme-challenge/";
        if (strncmp(request_buf, request_line_prefix, sizeof(request_line_prefix) - 1) != 0)
            fprintf(stderr, "<4>unrecognized mathod or URI: %s\n", request_buf);
        const char *const request_line_token = request_buf + sizeof(request_line_prefix) - 1;
        {
            char *const i = strchr(request_line_token, ' ');
            if (i)
                *i = 0;
            else
                fprintf(stderr, "<4>cannot find boundary of URI: %s\n", request_buf);
        }
        if (argc > 4 && strncmp(request_line_token, argv[4], strlen(argv[4])) != 0)
            fprintf(stderr, "<4>command line token %s does not match request: %s\n", argv[4], request_buf);

        char *response_body = (char[4096]){"TOKEN.ACCOUNT"};
        if (getenv("UACME_CHTTP_ACCOUNT"))
            snprintf(response_body, 4096, "%s.%s", request_line_token, getenv("UACME_CHTTP_ACCOUNT"));
        else if (argc > 5)
            response_body = argv[5];
        else
            fprintf(stderr, "<3>cannot determine challenge response\n");

        char response_header[4096];
        snprintf(response_header, sizeof(response_header), "HTTP/1.0 200 OK\r\n"
                                                           "Content-Type: text/plain\r\n"
                                                           "Content-Length: %ld\r\n"
                                                           "Connection: close\r\n"
                                                           "\r\n",
                 strlen(response_body));

        for (int i = 0, ret = strlen(response_header); i < ret; i += ensure_syscall("<2>send", send(client_fd, response_header + i, ret - i, MSG_MORE)))
            ;
        for (int i = 0, ret = strlen(response_body); i < ret; i += ensure_syscall("<2>send", send(client_fd, response_body + i, ret - i, MSG_MORE)))
            ;
        shutdown(client_fd, SHUT_WR);
        ensure_syscall("<2>close", close(client_fd));
    }

    freeaddrinfo(server_addr);
}