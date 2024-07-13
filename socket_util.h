#pragma once

#include <sys/types.h>
#include <stdint.h>

int uds_connect(const char* path);

typedef struct
{
    void (*free)(void* self);
    /**
     * The returned buffer is only valid until the next call to read.
     * Use handoff_buffer to give back the returned buffer.
     */
    ssize_t (*read)(void* self, int fd, uint8_t** p_buffer);
    void (*handoff_buffer)(void* self, uint8_t* buffer);
} stream_reader_t;

stream_reader_t* stream_reader_new();
