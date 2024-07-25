#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <tev/tev.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>
#include "tbus.h"

#ifdef USE_SIGNAL
#include <sys/eventfd.h>
#include <signal.h>
static void signal_handler(int signal);
static void signal_event_fd_read_handler(void* ctx);
static int signal_event_fd = -1;
#endif

static void on_message(const char* topic, const uint8_t* data, uint32_t len, void* ctx);

static tev_handle_t tev = NULL;
static tbus_t* tbus = NULL;

int main(int argc, char const *argv[])
{
    /** parse args */
    char* broker_path = NULL;
    char* topic = "#";
    int opt;
    while((opt = getopt(argc, (char**)argv, "p:t:h")) != -1)
    {
        switch(opt)
        {
            case 'p':
                broker_path = optarg;
                break;
            case 't':
                topic = optarg;
                break;
            case 'h':
                printf("Usage: %s [-p <broker_path>] [-t <topic>]\n", argv[0]);
                exit(EXIT_SUCCESS);
                break;
            default:
                break;
        }
    }

    tev = tev_create_ctx();
    assert(tev != NULL);
#ifdef USE_SIGNAL
    signal_event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    assert(signal_event_fd >= 0);
    assert(tev_set_read_handler(tev, signal_event_fd, signal_event_fd_read_handler, NULL) == 0);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif
    tbus = tbus_connect(tev, broker_path);
    assert(tbus != NULL);
    int rc = tbus->subscribe(tbus, topic, on_message, NULL);
    assert(rc == 0);

    tev_main_loop(tev);

    if(tbus)
        tbus->close(tbus);
#ifdef USE_SIGNAL
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    if(signal_event_fd >= 0)
    {
        tev_set_read_handler(tev, signal_event_fd, NULL, NULL);
        close(signal_event_fd);
    }
#endif
    tev_free_ctx(tev);
    return 0;
}

static void on_message(const char* topic, const uint8_t* data, uint32_t len, void* ctx)
{
    time_t now = time(NULL);
    char* time_str = ctime(&now);
    if(data)
        printf("%sTopic\n\t%s\nData[%"PRIu32"B]\n\t%.*s\n\n",
            time_str, topic, len, len, data);
    else
        printf("%sTopic\n\t%s\nData[0B]\n\t(null)\n\n",
            time_str, topic);
}

#ifdef USE_SIGNAL
static void signal_handler(int signal)
{
    eventfd_t value = 1;
    eventfd_write(signal_event_fd, value);
}

static void signal_event_fd_read_handler(void* ctx)
{
    eventfd_t value = 0;
    if(eventfd_read(signal_event_fd, &value) == -1)
        return;
    if(tbus)
    {
        tbus->close(tbus);
        tbus = NULL;
    }
    tev_set_read_handler(tev, signal_event_fd, NULL, NULL);
    close(signal_event_fd);
    signal_event_fd = -1;
}
#endif
