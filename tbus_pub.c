#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <tev/tev.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "tbus.h"

static void exit_loop(void* ctx)
{
    tbus_t* tbus = (tbus_t*)ctx;
    tbus->close(tbus);
}

int main(int argc, char const *argv[])
{
    /** parse args */
    char* broker_path = NULL;
    char* topic = NULL;
    char* message = NULL;
    int opt;
    while((opt = getopt(argc, (char**)argv, "p:t:m:hv")) != -1)
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
            case 'v':
                printf("Tbus library version: %s\n", tbus_get_version());
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
    if(!tev)
    {
        fprintf(stderr, "Failed to create tev context\n");
        exit(EXIT_FAILURE);
    }
    tbus_t* tbus = tbus_connect(tev, NULL);
    if(!tbus)
    {
        fprintf(stderr, "Failed to connect to tbus\n");
        exit(EXIT_FAILURE);
    }
    int rc = tbus->publish(tbus, topic, (uint8_t*)message, strlen(message));
    if(rc != 0)
    {
        fprintf(stderr, "Failed to publish message\n");
        exit(EXIT_FAILURE);
    }
    tev_timeout_handle_t timeout = tev_set_timeout(tev, exit_loop, tbus, 0);
    if(!timeout)
    {
        fprintf(stderr, "Failed to set exit timeout\n");
        exit(EXIT_FAILURE);
    }
    tev_main_loop(tev);
    tev_free_ctx(tev);
    return 0;
}
