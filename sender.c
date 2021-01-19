#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>


#define BUFFER_SIZE 4096


/**
 * Parameters to launch 'send_thread()' with.
 */
struct send_thread_params {
    int streamfd;
    const char *path;
    long part_size;
};


/**
 * Thread method to send a part of file to the client.
 *
 * This method will also close 'args->streamfd'.
 */
void *send_thread(void *args_raw)
{
    struct send_thread_params *args = (struct send_thread_params *)args_raw;

    FILE *file = fopen(args->path, "r");
    void *buff = malloc(BUFFER_SIZE);

    if (read(args->streamfd, buff, 1) != 1)
    {
        printf("Failed to read part identifier\n");
        free(buff);
        fclose(file);
        close(args->streamfd);
        return NULL;
    }
    unsigned char part = ((unsigned char *)buff)[0];

    fseek(file, args->part_size * part, SEEK_SET);
    long read_bytes = 0;
    while (true)
    {
        size_t size_to_read = read_bytes + BUFFER_SIZE > args->part_size ? args->part_size - read_bytes : BUFFER_SIZE;
        if (size_to_read == 0)
        {
            break;
        }
        ssize_t at_step_read_bytes = fread(buff, 1, size_to_read, file);
        if (at_step_read_bytes == 0)
        {
            break;
        }
        read_bytes += at_step_read_bytes;

        if (write(args->streamfd, buff, at_step_read_bytes) != at_step_read_bytes)
        {
            printf("Failed to send data via TCP: %s [%d]\n", strerror(errno), errno);
            break;
        }
    }

    free(buff);
    fclose(file);
    close(args->streamfd);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        printf("Usage: <port:int> <path:char*> <part_size:long> <threads:unsigned char>\n");
        return -1;
    }

    int port = (int)strtol(argv[1], NULL, 10);
    char *path = argv[2];
    long part_size = strtol(argv[3], NULL, 10);
    unsigned char threads = (unsigned char)strtol(argv[4], NULL, 10);

    /* Create a listening TCP connection */
    int sockfd = -1;
    {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
        {
            printf("Socket creation failed: %s [%d]\n", strerror(errno), errno);
            return -1;
        }

        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(struct sockaddr_in));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = htons(INADDR_ANY);
        bind_addr.sin_port = htons(port);
        if (bind(sockfd, (struct sockaddr *)&bind_addr, (socklen_t)sizeof(struct sockaddr_in)) != 0)
        {
            printf("Socket bind failed: %s [%d]\n", strerror(errno), errno);
            close(sockfd);
            return -1;
        }

        if (listen(sockfd, threads) != 0)
        {
            printf("Socket listen failed: %s [%d]\n", strerror(errno), errno);
            close(sockfd);
            return -1;
        }
    }

    /* Process requests */
    struct send_thread_params *pthread_params = malloc(sizeof(struct send_thread_params) * threads);
    pthread_t *pthreads = malloc(sizeof(pthread_t) * threads);
    int pthreads_i = 0;
    for (; pthreads_i < threads; pthreads_i++)
    {
        int streamfd = accept(sockfd, NULL, NULL);
        if (streamfd < 0)
        {
            printf("accept() failed: %s [%d]\n", strerror(errno), errno);
            break;
        }

        pthread_params[pthreads_i].path = path;
        pthread_params[pthreads_i].streamfd = streamfd;
        pthread_params[pthreads_i].part_size = part_size;

        int pthread_create_result = pthread_create(&pthreads[pthreads_i], NULL, send_thread, &pthread_params[pthreads_i]);
        if (pthread_create_result != 0)
        {
            printf("Thread creation failed: %s [%d]\n", strerror(pthread_create_result), pthread_create_result);
            break;
        }
    }

    for (int i = 0; i < pthreads_i; i++)
    {
        int pthread_join_result = pthread_join(pthreads[i], NULL);
        if (pthread_join_result != 0)
        {
            printf("Thread join failed for thread %d: %s [%d]\n", i, strerror(pthread_join_result), pthread_join_result);
        }
    }

    free(pthreads);
    free(pthread_params);

    close(sockfd);

    return 0;
}
