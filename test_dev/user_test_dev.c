#include "hpt.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>

#define IP_START_DEV 200

typedef struct devices_info
{
    int indx_dev;
    int count_buffers;
} devices_info_t;

void* hpt_new_dev(void* arg) 
{
    devices_info_t *new_dev = (devices_info_t *)arg;

    char cmd[256];
	snprintf(cmd, sizeof(cmd), 
                "xterm -hold -e '../test/user_test hpt%d %d 192.168.31.%d' &", 
                new_dev->indx_dev, 
                new_dev->count_buffers, 
                new_dev->indx_dev + IP_START_DEV);

    if (system(cmd) != 0) {
        printf("Failed open ./user_test hpt%d %d 192.168.31.%d\n", 
                new_dev->indx_dev, 
                new_dev->count_buffers, 
                new_dev->indx_dev + IP_START_DEV);
        return (void*)-1;
    }

    free(new_dev);
    return NULL;
}

int main(int argc, char* argv[]) 
{
    if (argc != 3) {
        printf("Invalid number of arguments\n");
        return 1;
    }

    const int count_dev = atoi(argv[1]);
    const int count_buffers = atoi(argv[2]);
    pthread_t threads[count_dev];

    if(count_dev == 0 || count_buffers == 0)
    {
        printf("Error HPT devices %d, buffers %d\n", count_dev, count_buffers);
        return 1;
    } 

    printf("Opened HPT devices %d, buffers %d\n", count_dev, count_buffers);

    for (int i = 0; i < count_dev; i++) 
    {
        devices_info_t *new_dev = malloc(sizeof(devices_info_t));
        new_dev->indx_dev = i;
        new_dev->count_buffers = count_buffers;

        if (pthread_create(&threads[i], NULL, hpt_new_dev, new_dev) != 0) {
            fprintf(stderr, "Failed to create thread for device %d\n", i);
            return 1;
        }
    }

    for (int i = 0; i < count_dev; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
