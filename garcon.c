#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#ifdef __linux__
#include <sys/sendfile.h>
#endif
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include "buffer/buffer.h"
#include "cmap/map.h"
#include "commander/commander.h"
#include "http_parser.h"

enum {
  time_buffer_size = 100
};

static const char default_filename[] = "index.html";

struct parser_data {
  buffer_t *url;
  char *client_address;
  struct tm timeinfo;
  struct map_t* headers;
  struct {
    buffer_t* key;
    buffer_t* val;
  } header; // TODO: rename 'header_bufs'
  int complete;
};

void parser_data_init(struct parser_data* data, const char* client_address) {
  data->url        = buffer_new();
  data->client_address = strdup(client_address);
  data->headers    = new_map();
  map_set_free_func(data->headers, free);
  data->header.key = buffer_new();
  data->header.val = buffer_new();
  data->complete = 0;

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

static int on_message_complete(http_parser* parser) {
  struct parser_data *data = parser->data;
  data->complete = 1;
  return 0;
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

const struct { const char* ext; const char* mime; } mime_types[] = {
  { "htm",   "text/html" },
  { "html",  "text/html" },
  { "js",    "text/javascript" },
  { "css",   "text/css" },
  { "svg",   "image/svg+xml" },
  { "jpg",   "image/jpeg" },
  { "jpeg",  "image/jpeg" },
  { "png",   "image/png" },
  { "gif",   "image/gif" },
  { "pdf",   "application/pdf" },
  { "xml",   "text/xml" },
  { "md",    "text/markdown" },
  { "csv",   "text/csv" },
  { "mp3",   "audio/mpeg" },
  { "zip",   "application/zip" },
  { "gz",    "application/gzip" },
  { "xhtml", "application/xhtml+xml" }
};

static void response_set_content_type(buffer_t* buffer, const char* uri)
{
  size_t i;
  const char* ext = strrchr(uri, '.');
  if (ext == NULL)
	  return;

  ext++;
  for (i = 0; i < sizeof(mime_types)/sizeof(mime_types[0]); ++i)
    if (strcasecmp(ext, mime_types[i].ext) == 0) {
      buffer_appendf(buffer, "Content-Type: %s\r\n", mime_types[i].mime);
      break;
    }
}

static buffer_t* response_headers(int length, int max_age, const struct tm *timeinfo, const char* uri)
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

  max_age = 31536000;
  buffer_appendf(result, "cache-control: public, max-age=%d\r\n", max_age);

  // Content - Length:459211

  response_set_content_type(result, uri);

  buffer_appendf(result, "Content-Length: %d\r\n", length);
  buffer_append(result, "Access-Control-Allow-Methods: GET\r\n");
  buffer_append(result, "Access-Control-Allow-Origin: *\r\n");
  buffer_append(result, "Server: Garcon 1.0\r\n");
  buffer_append(result, "\r\n");

  return result;
}

struct request {
  const char* uri;
  const char* user_agent;
  const char* method;
  const char* client_address;
  const struct tm* time;
};

static void log_request(int status, const struct request* request) {

  char time_buffer[time_buffer_size];
  const size_t time_size = strftime(time_buffer,
      time_buffer_size, "%FT%T%z", request->time);

  if (time_size == 0) {
    // TODO: use longjmp back to the main loop rather than quitting
    // the server
    fprintf(stderr, "Error formatting time");
    exit(EXIT_SUCCESS);
  }

  // Log request in Apache "Combined Log Format"
  // http://httpd.apache.org/docs/1.3/logs.html
  // <ip-address> <identd> <user> [<time>] "<request>" <status> <bytes> "<Referer>" "<User-agent>"
  // 123.65.150.10 - - [23/Aug/2010:03:50:59 +0000] "POST /wordpress3/wp-admin/admin-ajax.php HTTP/1.1" 200 2 "http://www.example.com/wordpress3/wp-admin/post-new.php" "Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10_6_4; en-US) AppleWebKit/534.3 (KHTML, like Gecko) Chrome/6.0.472.25 Safari/534.3"
  // 127.0.0.1 - frank [10/Oct/2000:13:55:36 -0700] "GET /apache_pb.gif HTTP/1.0" 200 2326 "http://www.example.com/start.html" "Mozilla/4.08 [en] (Win98; I ;Nav)"
  printf("%s - - [%s] %s %s %d \"%s\"\n",
      request->client_address,
      time_buffer,
      request->method,
      request->uri,
      status,
      request->user_agent);
}

static void send_error(int socket, int status, const struct request* request) {
  buffer_t* buffer = buffer_new();
  buffer_appendf(buffer, "http error %d", status);

  int age = 0;

  buffer_t* headers = response_headers(buffer_length(buffer), age, request->time, request->uri);
  int header_bytes_written = write(socket, headers->data, buffer_length(headers));
  buffer_free(headers);

  if (header_bytes_written == -1) {
    fputs("Error -1 writting error headers to socket\n", stderr);
  }

  int written = write(socket, buffer->data, buffer_length(buffer));
  if (written == -1){
    fprintf(stderr, "Error -1 writing error message to socket");
  }
  buffer_free(buffer);
  log_request(status, request);
}

static int buffer_endswith_char(buffer_t *self, char ch) {
  size_t len = buffer_length(self);
  return len > 0 && buffer_string(self)[len-1] == ch;
}

void remove_query_string(buffer_t **path) {
  const ssize_t pos = buffer_indexof(*path, "?");

  if (pos == -1) {
    return;
  }

  buffer_t *result = buffer_slice(*path, 0, pos);
  buffer_free(*path);
  *path = result;
}

// Return 1 if the given file descriptor
// is a regular file, and 0 otherwise.
static int is_regular_file(int fd, off_t *file_length) {
  struct stat stat;
  const int stat_res = fstat(fd, &stat);
  assert(stat_res == 0);
  *file_length = stat.st_size;
  return S_ISREG(stat.st_mode);
}

static ssize_t send_file_to_socket(const int fd, const int socket, off_t length) {
#ifdef __linux__
  off_t off = 0;
  return sendfile(socket, fd, &off, length);
#else
  return sendfile(fd, socket, 0, &length, 0, 0);
#endif
}


static void send_file(
    int socket,
    const char *root,
    const struct request *request)
{
  buffer_t *buffer = buffer_new();
  buffer_append(buffer, root);
  buffer_append(buffer, request->uri);
  remove_query_string(&buffer);
  if (buffer_endswith_char(buffer, '/')) {
    buffer_append(buffer, default_filename);
  }

  int file = open(buffer->data, O_RDONLY);
  if (file <= 0) {
    send_error(socket, 404, request);
    return;
  }
  buffer_free(buffer);

  off_t file_length;
  if (!is_regular_file(file, &file_length)) {
    send_error(socket, 403, request);
    return;
  }

  // TODO set a max age header
  {
    buffer_t *headers = response_headers(file_length, 0, request->time, request->uri);
    write(socket, headers->data, buffer_length(headers));
    buffer_free(headers);
  }

  ssize_t result = send_file_to_socket(file, socket, file_length);
  close(file);

  if (result == -1) {
    perror("sendfile");
    return;
  }
  log_request(200, request);
}

int open_connection(int port)
{
  const int create_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (create_socket < 1) {
    perror("opening socket\n");
    exit(EXIT_FAILURE);
  }

  const int yes = 1;
  if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

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
  long int port;
};

static void init_options(struct options* options) {
  options->port = 8888;
  options->root = getcwd(0, 0);
}

static void set_root(command_t *self) {
  struct options* options = self->data;
  options->root = realpath(self->arg, NULL);
  if (!options->root) {
    fprintf(stderr, "Cannot find directory %s", self->arg);
    exit(EXIT_FAILURE);
  }
}

static void set_port(command_t *self) {
  struct options* options = self->data;
  char* endptr = 0;
  options->port = strtol(self->arg, &endptr, 10);
  if (*endptr) {
    fprintf(stderr, "Error: invalid port number: %s\n", self->arg);
    exit(EXIT_FAILURE);
  }

  if (options->port > USHRT_MAX) {
    fprintf(stderr, "Error: port number %ld is larger than the max %d\n",
        options->port,
        USHRT_MAX);
    exit(EXIT_FAILURE);
  }
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

  signal(SIGPIPE, SIG_IGN);

  printf("Garçon! Serving content from %s on http://localhost:%ld/\n", options.root, options.port);

  const int socket = open_connection(options.port);

  http_parser_settings settings;
  memset(&settings, 0, sizeof(http_parser_settings));
  settings.on_url = on_url;
  settings.on_header_value = on_header_value;
  settings.on_header_field = on_header_field;
  settings.on_message_complete = on_message_complete;

  if (listen(socket, 10) < 0) {
    perror("server: listen");
    exit(EXIT_FAILURE);
  }

    struct sockaddr_in address;
    socklen_t addrlen;

http:
  for(;;) {
    //xkcd.com/292/

    const int new_socket = accept(socket, (struct sockaddr *)&address, &addrlen);

    if (new_socket <= 0) {
      perror("accept");
      exit(EXIT_FAILURE);
    }

    struct parser_data data;
    parser_data_init(&data, inet_ntoa(address.sin_addr));

    // TODO: anything to free()?
    http_parser parser;
    http_parser_init(&parser, HTTP_REQUEST);
    parser.data = &data;

    for (;;) {

      const int len = 1024;
      char buf[len];

      // Read data from the socket
      const ssize_t recved = recv(new_socket, buf, len, 0);

      if (recved == 0) {
        break;
      }

      if (recved == -1) {
        perror("Error reading request from network");
        goto http;
      }
      // Start up / continue the parser. Note we pass recved==0 to
      // signal that EOF has been received.
      const ssize_t nparsed = http_parser_execute(&parser, &settings, buf, recved);

      if (nparsed != recved) {
        fprintf(stderr, "Error parsing HTTP header\n");
        goto http;
      }

      if (data.complete || http_body_is_final(&parser)) {
        break;
      }
    }


     const int headers = 0;

    if (headers) {
      puts("Headers");
      for (struct map_node_t* node = data.headers->head; node; node = node->next) {
        if (node->value) {
          printf("%s: %s\n", node->key, (const char*)node->value);
        }
      }
    }

    struct request request;
    request.user_agent = map_get(data.headers, "User-Agent");
    request.client_address = data.client_address;
    request.time = &data.timeinfo;
    request.uri = data.url->data;
    request.method = http_method_str(parser.method);

    if (parser.http_errno) {
      request.user_agent = "";
      request.method = "";
      request.uri = "BAD REQUEST";
      send_error(new_socket, 400, &request);
    } else if (parser.method != HTTP_GET) {
      send_error(new_socket, 405, &request);
    } else if (parser.upgrade) {
      /* handle new protocol */
    } else {
      send_file(new_socket, options.root, &request);
    }
    parser_data_destroy(&data);
    shutdown(new_socket, SHUT_RDWR);
    close(new_socket);
  }
  close(socket);

  return EXIT_SUCCESS;
}

