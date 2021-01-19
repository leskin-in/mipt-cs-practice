/*
 * This is supposed to be an internal utility, never run by user directly. Thus
 * no correctness checks are made.
 *
 * This must be fixed in "production" version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>


/* Maximum length of "part file" name */
#define MAXIMUM_PART_FILE_NAME_LENGTH 16

/* Maximum buffer size */
#define BUFFER_SIZE 4096


/**
 * Create a set of TCP sockets, connected to 'host:port', and store them in
 * 'socket_array'.
 *
 * Returns 0 on failure, 1 on success.
 *
 * Caller's responsibilities:
 * * Provide correct 'socket_array'
 * * Close opened and connected sockets in the resulting array if '-1' is
 * returned (multiple sockets may be left open)
 */
static int connect_sockets(char *host, char *port, int *socket_array, int socket_array_l)
{
     struct addrinfo hints;
     memset(&hints, 0, sizeof(struct addrinfo));
     hints.ai_family = AF_UNSPEC;
     hints.ai_socktype = SOCK_STREAM;

     struct addrinfo *ais;
     int gai_result = getaddrinfo(host, port, &hints, &ais);
     if (gai_result != 0)
     {
          printf("Failed to resolve server address: %s [%d]\n", gai_strerror(gai_result), gai_result);
          return 0;
     }

     /* The first addrinfo is supposed to be correct at all times. This is an assumption; in production it may be incorrect */
     for (int i = 0; i < socket_array_l; i++)
     {
          struct addrinfo *ais_it;
          bool connected = false;
          for (ais_it = ais; ais_it != NULL; ais_it = ais_it->ai_next)
          {
               socket_array[i] = socket(ais_it->ai_family, ais_it->ai_socktype, ais_it->ai_protocol);
               if (socket_array[i] < 0)
               {
                    continue;
               }
               
               if (connect(socket_array[i], ais_it->ai_addr, ais_it->ai_addrlen) != 0)
               {
                    continue;
               }
               connected = true;
               break;
          }

          if (!connected)
          {
               /* This only shows the last error... */
               printf("Failed to connect: %s [%d]\n", strerror(errno), errno);
               return -1;
          }
     }

     return 1;
}

/**
 * Parameters to launch 'recieve_thread()' with.
 */
struct receive_thread_params {
     unsigned char thread_id;
     int socket;
     const char *path;
};

/**
 * Thread method to receive binary data from a socket.
 *
 * Returns integer '1' when finished successfully, 'NULL' otherwise.
 *
 * The socket must be already open. It is not managed by thread itself.
 */
void *receive_thread(void *args_raw)
{
     struct receive_thread_params *args = (struct receive_thread_params *)args_raw;

     const int resulting_path_l = strlen(args->path) + 16;
     char *resulting_path = malloc(resulting_path_l);
     snprintf(resulting_path, resulting_path_l, "%s/%d.prt", args->path, args->thread_id);

     FILE *file = fopen(resulting_path, "w");
     void *buff = malloc(BUFFER_SIZE);

     /* Send my identifier */
     ((unsigned char *)buff)[0] = args->thread_id;
     if (write(args->socket, buff, 1) != 1)
     {
          printf("Send failure in thread %d\n", args->thread_id);
          free(buff);
          fclose(file);
          free(resulting_path);
          return NULL;
     }

     while (1)
     {
          ssize_t received = read(args->socket, buff, BUFFER_SIZE);
          if (received == 0)
          {
               /* Orderly shutdown */
               break;
          }
          if (received == -1)
          {
               printf("Receive failure in thread %d: %s [%d]\n", args->thread_id, strerror(errno), errno);
               free(buff);
               fclose(file);
               free(resulting_path);
               return NULL;
          }
          fwrite(buff, received, 1, file);
     }
     fflush(file);

     free(buff);
     fclose(file);
     free(resulting_path);

     return (void *)1;
}

int main(int argc, char **argv)
{
     if (argc != 5)
     {
          printf("Usage: <host:char*> <port:int> <threads:unsigned char> <path:char*>\n");
          return -1;
     }

     char *host = argv[1];
     char *port = argv[2];
     unsigned char threads = (unsigned char)strtol(argv[3], NULL, 10);
     char *path = argv[4]; /* is a directory without trailing '/' */

     /* Create sockets */
     int *sockets = malloc(sizeof(int) * threads);
     for (int i = 0; i < threads; i++)
     {
          sockets[i] = -1;
     }
     if (!connect_sockets(host, port, sockets, threads))
     {
          for (int i = 0; i < threads; i++)
          {
               if (sockets[i] < 0)
               {
                    continue;
               }
               close(sockets[i]);
          }
          free(sockets);
          return -1;
     }

     /* Launch threads */
     struct receive_thread_params *thread_params = malloc(sizeof(struct receive_thread_params) * threads);
     pthread_t *pthreads = malloc(sizeof(pthread_t) * threads);
     for (int i = 0; i < threads; i++)
     {
          thread_params[i].thread_id = i;
          thread_params[i].socket = sockets[i];
          thread_params[i].path = path;

          int pthread_create_result = pthread_create(&pthreads[i], NULL, receive_thread, &thread_params[i]);
          if (pthread_create_result != 0)
          {
               printf("Thread creation failed for thread %d: %s [%d]\n", i, strerror(pthread_create_result), pthread_create_result);
               /*
                * This is bad, but whatever... This is a proof-of-concept
                * solution. The good solution requires us to stop the threads
                * that are receiving data (e.g. by means of mutex), so that
                * they do not use sockets we are going to close.
                *
                * Another option is to close sockets in threads. But this still
                * requires extra code which is unlikely to be necessary, except
                * in exceptional circumstances.
                */
               exit(1);
          }
     }

     /* Join threads and close their sockets */
     bool join_failed = false;
     for (int i = 0; i < threads; i++)
     {
          void *thread_result;
          int pthread_join_result = pthread_join(pthreads[i], &thread_result);
          if (pthread_join_result != 0)
          {
               printf("Thread join failed for thread %d: %s [%d]\n", i, strerror(pthread_join_result), pthread_join_result);
               join_failed = true;
          }
          close(sockets[i]);
     }

     free(pthreads);
     free(thread_params);
     free(sockets);

     if (join_failed)
     {
          return -1;
     }
     return 0;
}
