#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <fcntl.h>

#include "http_parser.h"
#include "buffer.h"

struct parser_data {
  buffer_t* url;
};

static int on_url(http_parser* parser, const char *at, size_t len)
{
  struct parser_data* data = parser->data;
  buffer_append_n(data->url, at, len);
  printf("Append %zu bytes to URL\n", len);
  return 0;
}

static void send_file(int socket, const char* root, const char* filename) {


  buffer_t* buffer = buffer_new();
  buffer_append(buffer, root);
  buffer_append(buffer, filename);

  int file = open(buffer->data, O_RDONLY);
  if (file <= 0) {
    fprintf(stderr, "Cannot open %s\n", filename);
    return;
  }

  // Whole file
  off_t length = 0; 
  int result = sendfile(file, socket, 0, &length, 0, 0);

  if (result == -1) {
    return;
  }
  // READ FILE
  close(file);
}

int main(int argc, char** argv) {


  (void)argc;
  (void)argv;
  puts("Garçon!");

  const uint16_t port = 8888;


  const char* root = getcwd(0, 0);
  printf("Serving content from %s\n", root);

   
   socklen_t addrlen;
   struct sockaddr_in address;

    int create_socket;
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) > 0){
      printf("The socket was created\n");
   }

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons(port);

  int yes = 1;
  if ( setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1 ) {
    perror("setsockopt");
  } 
    
   if (bind(create_socket, (struct sockaddr *) &address, sizeof(address)) == 0){
      printf("Binding socket on port %d\n", port);
   } else {
    perror("bind failed");
   }

   http_parser_settings settings;
   memset(&settings, 0, sizeof(http_parser_settings));
   settings.on_url = on_url;

   while (1) {
      if (listen(create_socket, 10) < 0) {
         perror("server: listen");
         exit(1);
      }

      int new_socket;

      if ((new_socket = accept(create_socket, (struct sockaddr *) &address, &addrlen)) < 0) {
         perror("server: accept");
         exit(1);
      }



      if (new_socket > 0){
         printf("The Client is connected...\n");
      }

      size_t len = 80*1024;
      char buf[len];

      ssize_t recved = recv(new_socket, buf, len, 0);

      if (recved < 0) {
        /* Handle error. */
      }

      struct parser_data data;
      data.url = buffer_new();

      http_parser *parser = malloc(sizeof(http_parser));
      http_parser_init(parser, HTTP_REQUEST);
      parser->data = &data;

      /* Start up / continue the parser.
       * Note we pass recved==0 to signal that EOF has been received.
       */
      size_t nparsed = http_parser_execute(parser, &settings, buf, recved);

      if (parser->upgrade) {
        /* handle new protocol */
      } else if (nparsed != recved) {
        /* Handle error. Usually just close the connection. */
      }

      const char* path = data.url->data;
      send_file(new_socket, root, path);

      free(parser);
      buffer_free(data.url);
      close(new_socket);
   }
   close(create_socket);

  return EXIT_SUCCESS;
}

