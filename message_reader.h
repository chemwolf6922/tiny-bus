#pragma once

#include <tev/tev.h>
#include "message.h"

typedef struct message_reader_s message_reader_t;
struct message_reader_s
{
    void (*close)(message_reader_t* self);
    char* (*take_over_buffer)(message_reader_t* self, size_t* size);
    struct
    {
        void (*on_message)(const tbus_message_t* msg, void* ctx);
        void* on_message_ctx;
        void (*on_error)(void* ctx);
        void* on_error_ctx;
    } callbacks;
};

message_reader_t* message_reader_new(tev_handle_t tev, int fd);
