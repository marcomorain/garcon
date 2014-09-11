#include <arpa/inet.h>
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
#include <time.h>
#include <unistd.h>
#include "buffer.h"
#include "map.h"
#include "commander.h"
#include "http_parser.h"

enum {
    time_buffer_size = 100
};

struct parser_data {
    buffer_t *url;
    char *client_address;
    struct tm timeinfo;
    struct map_t* headers;
    struct {
        buffer_t* key;
        buffer_t* val;
    } header;
};

void parser_data_init(struct parser_data* data, const char* client_address) {
    data->url        = buffer_new();
    data->client_address = strdup(client_address);
    data->headers    = new_map();
    map_set_free_func(data->headers, free);
    data->header.key = buffer_new();
    data->header.val = buffer_new();

    // Log the current time
    time_t rawtime;
    time(&rawtime);
    gmtime_r(&rawtime, &data->timeinfo);
}

void parser_data_destroy(struct parser_data* data) {
    buffer_free(data->url);
    free(data->client_address);
    destroy_map(&data->headers);
    buffer_free(data->header.key);
    buffer_free(data->header.val);
    memset(&data, 0, sizeof(data));
}

static void version(FILE* file) {
    const unsigned long version = http_parser_version();
    const unsigned major = (version >> 16) & 255;
    const unsigned minor = (version >> 8) & 255;
    const unsigned patch = version & 255;
    fprintf(file, "http_parser v%u.%u.%u\n", major, minor, patch);
}

static int on_header_field(http_parser * parser, const char *at, size_t len) {
    struct parser_data *data = parser->data;

    if (buffer_length(data->header.val) > 0) {
        map_set(data->headers,  strdup(data->header.key->data),
                strdup(data->header.val->data));
        buffer_clear(data->header.key);
        buffer_clear(data->header.val);
    }
    buffer_append_n(data->header.key, at, len);
    return 0;
}

static int on_header_value(http_parser * parser, const char *at, size_t len) {
    struct parser_data *data = parser->data;
    buffer_append_n(data->header.val, at, len);
    return 0;
}

static int on_url(http_parser * parser, const char *at, size_t len) {
    struct parser_data *data = parser->data;
    buffer_append_n(data->url, at, len);
    return 0;
}

static buffer_t* response_headers(int length, int max_age, struct tm *timeinfo)
{
    buffer_t *result = buffer_new();
    buffer_append(result, "HTTP/1.1 200 OK\r\n");


    char time_buffer[time_buffer_size];
    const size_t time_size = strftime(time_buffer, time_buffer_size, "%a, %d %h %Y %T %Z", timeinfo);

    if (time_size == 0) {
        fprintf(stderr, "Error formatting time");
        exit(EXIT_SUCCESS);
    }

    // Date: Sun, 07 Sep 2014 14: 51:17 GMT
    buffer_appendf(result, "Date: %s\r\n", time_buffer);

    // TODO:
    // Last-Modified: Sun, 07 Sep 2014 01: 37:26 GMT
    // Content-Type:image / gif

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


static void send_file(int socket, const char *root, const char *filename, const char* user_agent, const char* client_address, struct tm *timeinfo)
{
    buffer_t *buffer = buffer_new();
    buffer_append(buffer, root);
    buffer_append(buffer, filename);
    int file = open(buffer->data, O_RDONLY);
    buffer_free(buffer);
    if (file <= 0) {
        fprintf(stderr, "Cannot open %s\n", filename);
        // TODO: 404
        return;
    }
    struct stat stat;
    int stat_res = fstat(file, &stat);
    assert(stat_res == 0);
    // TODO max age
    buffer_t * headers = response_headers(stat.st_size, 0, timeinfo);

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

    char time_buffer[time_buffer_size];
    const size_t time_size = strftime(time_buffer, time_buffer_size, "%FT%T%z", timeinfo);

    if (time_size == 0) {
        // TODO: long jump instead
        fprintf(stderr, "Error formatting time");
        exit(EXIT_SUCCESS);
    }

    // Log request in Apache "Combined Log Format"
    // http://httpd.apache.org/docs/1.3/logs.html
    // <ip-address> <identd> <user> [<time>] "<request>" <status> <bytes> "<Referer>" "<User-agent>"
    // 123.65.150.10 - - [23/Aug/2010:03:50:59 +0000] "POST /wordpress3/wp-admin/admin-ajax.php HTTP/1.1" 200 2 "http://www.example.com/wordpress3/wp-admin/post-new.php" "Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.472.25 Safari/534.3"
    // 127.0.0.1 - frank [10/Oct/2000:13:55:36 -0700] "GET /apache_pb.gif HTTP/1.0" 200 2326 "http://www.example.com/start.html" "Mozilla/4.08 [en] (Win98; I ;Nav)"
    //

    printf("%s - - [%s] GET %s 200 \"%s\"\n", client_address, time_buffer, filename, user_agent);
    close(file);
}

int open_connection(int port)
{
    const int create_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (create_socket < 1) {
        perror("opening socket\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    const int yes = 1;
    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    const int bound = bind(create_socket, (struct sockaddr *)&address, sizeof(address));

    if (bound != 0) {
        fprintf(stderr, "Failed to bind to port %d", port);
        perror(" ");
        exit(EXIT_FAILURE);
    }

    return create_socket;
}


struct options {
    char* root;
    int port;
};

static void init_options(struct options* options) {
    options->port = 8888;
    options->root = getcwd(0, 0);
}

static void set_root(command_t *self) {
    struct options* options = self->data;
    // TODO Error checking of folder (stat)
    options->root = realpath(self->arg, NULL);
    if (!options->root) {
        fprintf(stderr, "Cannot find directory %s", self->arg);
        exit(EXIT_FAILURE);
    }
}

static void set_port(command_t *self) {
    struct options* options = self->data;
    // TODO Error checking of port
    options->port = atoi(self->arg);
    
}

int main(int argc, char **argv)
{
    struct options options;
    init_options(&options);

    command_t cmd;
    cmd.data = &options;
    command_init(&cmd, argv[0], "0.0.1");
    command_option(&cmd, "-d", "--directory [arg]", "The root directory to serve files from (default to the current working directory)", set_root);
    command_option(&cmd, "-p", "--port [arg]", "Which port to listen on (default 8888)", set_port);
    command_parse(&cmd, argc, argv);

    
    printf("Garçon! Serving content from %s on port %d\n", options.root, options.port);
    version(stdout);

    const int socket = open_connection(options.port);

    http_parser_settings settings;
    memset(&settings, 0, sizeof(http_parser_settings));
    settings.on_url = on_url;
    settings.on_header_value = on_header_value;
    settings.on_header_field = on_header_field;

    for(;;)
    {
        if (listen(socket, 10) < 0) {
            perror("server: listen");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in address;
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
        parser_data_init(&data, inet_ntoa(address.sin_addr));

        http_parser *parser = malloc(sizeof(http_parser));
        http_parser_init(parser, HTTP_REQUEST);
        parser->data = &data;

        /* Start up / continue the parser. Note we pass recved==0 to
           signal that EOF has been received. */
        /*const size_t nparsed =*/ http_parser_execute(parser, &settings, buf, recved);

        // printf("Headers\n");
        // for (struct map_node_t* node = data.headers->head; node; node = node->next) {
        //     if (node->value) {
        //         printf("%s: %s\n", node->key, node->value);
        //     }
        // }

        if (parser->http_errno) {
            perror("http error");
        }

        if (parser->method != HTTP_GET) {
            // TODO: method not supported
            fprintf(stderr, "%s method is not supported\n", http_method_str(parser->method));
        }

        if (parser->upgrade) {
            /* handle new protocol */
            //} else if (nparsed != recved) {
            /* Handle error. Usually just close the connection. */
        }

        // TODO: assert method is GET
        const char *path = data.url->data;

        send_file(new_socket, options.root, path, map_get(data.headers, "User-Agent"), data.client_address, &data.timeinfo);

        parser_data_destroy(&data);
        free(parser);
        shutdown(new_socket, SHUT_RDWR);
        close(new_socket);
    }
    close(socket);

    return EXIT_SUCCESS;
}
