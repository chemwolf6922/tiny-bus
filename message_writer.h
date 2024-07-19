#pragma once

#include <tev/tev.h>
#include "message.h"

typedef struct message_writer_s message_writer_t;
struct message_writer_s
{
    void (*close)(message_writer_t* self);
    int (*write_message)(message_writer_t* self, const tbus_message_t* msg);
    struct
    {
        void (*on_error)(void* ctx);
        void* on_error_ctx;
    } callbacks;
};

message_writer_t* message_writer_new(tev_handle_t tev, int fd);
