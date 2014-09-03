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
#include "http_parser.h"


char url_buffer[1024];

static int on_url(http_parser* parser, const char *at, size_t len)
{
  assert(len < 1024);
  memcpy(url_buffer, at, len);
  url_buffer[len] = 0;
  (void)parser;
  printf("URL>>>> '%s'\n", url_buffer);
  return 0;
}

static void send_file(int socket, char* filename) {
  FILE* file = fopen(filename, "rb");
  if (!file) {
    fprintf(stderr, "Cannot open %s", filename);
    return;
  }
// READ FILE
  fclose(file);
}

int main(int argc, char** argv) {


  (void)argc;
  (void)argv;
  puts("Garçon!");

   int create_socket, new_socket;
   socklen_t addrlen;
   struct sockaddr_in address;

   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) > 0){
      printf("The socket was created\n");
   }

   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons(15000);

   if (bind(create_socket, (struct sockaddr *) &address, sizeof(address)) == 0){
      printf("Binding Socket\n");
   }

   http_parser_settings settings;
   memset(&settings, 0, sizeof(http_parser_settings));
   settings.on_url = on_url;

   while (1) {
      if (listen(create_socket, 10) < 0) {
         perror("server: listen");
         exit(1);
      }

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

      http_parser *parser = malloc(sizeof(http_parser));
      http_parser_init(parser, HTTP_REQUEST);
      parser->data = new_socket;

      /* Start up / continue the parser.
       * Note we pass recved==0 to signal that EOF has been received.
       */
      size_t nparsed = http_parser_execute(parser, &settings, buf, recved);

      printf("Parsed %x\n", nparsed);

      free(parser);

      if (parser->upgrade) {
        /* handle new protocol */
      } else if (nparsed != recved) {
        /* Handle error. Usually just close the connection. */
      }

      write(new_socket, "hello world\n", 12);
      close(new_socket);
   }
   close(create_socket);

  return EXIT_SUCCESS;
}

