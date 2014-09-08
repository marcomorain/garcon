#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "buffer.h"
#include "http_parser.h"

struct parser_data {
    buffer_t *url;
};

static void version(FILE* file) {
    const unsigned long version = http_parser_version();
    const unsigned major = (version >> 16) & 255;
    const unsigned minor = (version >> 8) & 255;
    const unsigned patch = version & 255;
    fprintf(file, "http_parser v%u.%u.%u\n", major, minor, patch);
}

static int on_header_value(http_parser * parser, const char *at, size_t len) {
    (void)parser;
    for (size_t i=0; i<len; i++) {
        putchar(at[i]);
    }
    puts("");
    return 0;
}

static int on_header_field(http_parser * parser, const char *at, size_t len) {
    (void)parser;
    return on_header_value(parser, at, len);
}

static int on_url(http_parser * parser, const char *at, size_t len)
{
    struct parser_data *data = parser->data;
    buffer_append_n(data->url, at, len);
    return 0;
}

static buffer_t* response_headers(int length, int max_age)
{
    buffer_t *result = buffer_new();
    buffer_append(result, "HTTP/1.1 200 OK\r\n");

    // <Date: Sun, 07 Sep 2014 14: 51:17 GMT
    // <Last - Modified: Sun, 07 Sep 2014 01: 37:26 GMT
    // return (strftime(tmbuf, len, "%a, %d %h %Y %T %Z", &tm));

    // <Content - Type:image / gif
    max_age = 31536000;
    buffer_appendf(result, "cache-control: public, max-age=%d\r\n", max_age);

    // Content - Length:459211
    buffer_appendf(result, "Content-Length: %d\r\n", length);
    buffer_append(result, "Access-Control-Allow-Methods: GET\r\n");
    buffer_append(result, "Access-Control-Allow-Origin: *\r\n");
    buffer_append(result, "Server: Garcon 1.0\r\n");
    buffer_append(result, "\r\n");

    return result;
}

static void send_file(int socket, const char *root, const char *filename)
{
    buffer_t *buffer = buffer_new();
    buffer_append(buffer, root);
    buffer_append(buffer, filename);
    int file = open(buffer->data, O_RDONLY);
    buffer_free(buffer);
    if (file <= 0) {
        fprintf(stderr, "Cannot open %s\n", filename);
        return;
    }
    struct stat stat;
    int stat_res = fstat(file, &stat);
    assert(stat_res == 0);
    // TODO max age
    buffer_t * headers = response_headers(stat.st_size, 0);

    write(socket, headers->data, buffer_length(headers));

    buffer_free(headers);

    off_t file_length = stat.st_size;

    int result = sendfile(file, socket, 0, &file_length, 0, 0);

    if (file_length != stat.st_size) {
        printf("sendfile data length error\n");
    }

    if (result == -1) {
        perror("sendfile");
        return;
    }

// 123.65.150.10 - - [23/Aug/2010:03:50:59 +0000] "POST /wordpress3/wp-admin/admin-ajax.php HTTP/1.1" 200 2 "http://www.example.com/wordpress3/wp-admin/post-new.php" "Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.472.25 Safari/534.3"

    printf("<client ip> - - [time] <GET> %s 200\n", filename);
    close(file);
}

int open_connection(uint16_t port, struct sockaddr_in* address)
{
    const int create_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (create_socket < 1) {
        perror("opening socket\n");
        exit(EXIT_FAILURE);
    }

    address->sin_family = AF_INET;
    address->sin_addr.s_addr = INADDR_ANY;
    address->sin_port = htons(port);

    const int yes = 1;
    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    const int bound = bind(create_socket, (struct sockaddr *)address, sizeof(*address));

    if (bound != 0) {
        fprintf(stderr, "Failed to bind to port %d", port);
        perror(" ");
        exit(EXIT_FAILURE);
    }

    return create_socket;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const uint16_t port = 8888;

    const char *root = getcwd(0, 0);
    printf("Garçon! Serving content from %s on port %d\n", root, port);
    version(stdout);
    
    struct sockaddr_in address;
    
    const int socket = open_connection(port, &address);

    http_parser_settings settings;
    memset(&settings, 0, sizeof(http_parser_settings));
    settings.on_url = on_url;
    settings.on_header_value = on_header_value;
    settings.on_header_field = on_header_field;


    while (1) {
        if (listen(socket, 10) < 0) {
            perror("server: listen");
            exit(EXIT_FAILURE);
        }

        socklen_t addrlen;
        const int new_socket = accept(socket, (struct sockaddr *)&address, &addrlen);

        if (new_socket <= 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        size_t len = 80 * 1024;
        char buf[len];

        const ssize_t recved = recv(new_socket, buf, len, 0);

        if (recved < 0) {
            /* Handle error. */
        }
        struct parser_data data;
        data.url = buffer_new();

        http_parser *parser = malloc(sizeof(http_parser));
        http_parser_init(parser, HTTP_REQUEST);
        parser->data = &data;

        /* Start up / continue the parser. Note we pass recved==0 to
           signal that EOF has been received. */
        /*const size_t nparsed =*/ http_parser_execute(parser, &settings, buf, recved);

        if (parser->http_errno) {
            perror("http error");
        }

        if (parser->upgrade) {
            /* handle new protocol */
            //} else if (nparsed != recved) {
            /* Handle error. Usually just close the connection. */
        }
        const char *path = data.url->data;
        send_file(new_socket, root, path);

        free(parser);
        buffer_free(data.url);
        shutdown(new_socket, SHUT_RDWR);
        close(new_socket);
    }
    close(socket);

    return EXIT_SUCCESS;
}
