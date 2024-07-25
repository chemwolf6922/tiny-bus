#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <tev/tev.h>
#include <assert.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "tbus.h"

int main(int argc, char const *argv[])
{
    /** parse args */
    char* broker_path = NULL;
    char* topic = NULL;
    char* message = NULL;
    int opt;
    while((opt = getopt(argc, (char**)argv, "p:t:m:")) != -1)
    {
        switch(opt)
        {
            case 'p':
                broker_path = optarg;
                break;
            case 't':
                topic = optarg;
                break;
            case 'm':
                message = optarg;
                break;
            case 'h':
                printf("Usage: %s [-p <broker_path>] [-t <topic>] [-m <message>]\n", argv[0]);
                exit(EXIT_SUCCESS);
                break;
            default:
                break;
        }
    }
    if(!topic || !message)
    {
        fprintf(stderr, "Usage: %s -t <topic> -m <message> [-p <broker_path>]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /** publish message */
    tev_handle_t tev = tev_create_ctx();
    assert(tev != NULL);
    tbus_t* tbus = tbus_connect(tev, NULL);
    assert(tbus != NULL);
    int rc = tbus->publish(tbus, topic, (uint8_t*)message, strlen(message));
    assert(rc == 0);
    tbus->close(tbus);
    tev_free_ctx(tev);
    return 0;
}
