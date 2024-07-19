#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include "message_reader.h"

// Fit the buffer in one page
#define STATIC_BUFFER_SIZE (4000)

typedef struct
{
    message_reader_t iface;
    tev_handle_t tev;   
    int fd;
    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_offset;
} message_reader_impl_t;

static void message_reader_close(message_reader_t* self);
static uint8_t* message_reader_take_over_buffer(message_reader_t* self, size_t* size);
static void read_handler(void* ctx);
static void error_handler(message_reader_impl_t* this);

message_reader_t* message_reader_new(tev_handle_t tev, int fd)
{
    if(!tev || fd < 0)
        return NULL;
    message_reader_impl_t* this = malloc(sizeof(message_reader_impl_t));
    if (this == NULL)
        goto error;
    memset(this, 0, sizeof(message_reader_impl_t));
    this->iface.close = message_reader_close;
    this->iface.take_over_buffer = message_reader_take_over_buffer;
    this->tev = tev;
    this->fd = fd;
    this->buffer_size = STATIC_BUFFER_SIZE;
    this->buffer = malloc(this->buffer_size);
    if(!this->buffer)
        goto error;
    if(tev_set_read_handler(tev, fd, read_handler, this) != 0)
        goto error;
    return (message_reader_t*)this;
error:
    message_reader_close((message_reader_t*)this);
    return NULL;
}

static void message_reader_close(message_reader_t* self)
{
    message_reader_impl_t* this = (message_reader_impl_t*)self;
    if(!this)
        return;
    if(this->fd >= 0 && this->tev)
        tev_set_read_handler(this->tev, this->fd, NULL, NULL);
    if(this->buffer)
        free(this->buffer);
    free(this);
}

static uint8_t* message_reader_take_over_buffer(message_reader_t* self, size_t* size)
{
    message_reader_impl_t* this = (message_reader_impl_t*)self;
    if(!this)
        return;
    uint8_t* new_buffer = malloc(STATIC_BUFFER_SIZE);
    if(!new_buffer)
        return;
    uint8_t* old_buffer = this->buffer;
    *size = this->buffer_offset;
    this->buffer = new_buffer;
    this->buffer_size = STATIC_BUFFER_SIZE;
    this->buffer_offset = 0;
error:
    *size = 0;
    return NULL;
}

static void read_handler(void* ctx)
{
    message_reader_impl_t* this = (message_reader_impl_t*)ctx;
    /** read len first */
    tbus_message_len_t msg_len;
    if(this->buffer_offset < sizeof(tbus_message_len_t))
    {
        ssize_t read_len = read(
            this->fd, 
            this->buffer + this->buffer_offset, 
            sizeof(tbus_message_len_t) - this->buffer_offset);
        switch(read_len)
        {
            case -1:
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                    return;
                /** Error */
                error_handler(this);
                return;
            case 0:
                /** EOF */
                error_handler(this);
                return;
            default:
                break;
        }
        this->buffer_offset += read_len;
        if(this->buffer_offset < sizeof(tbus_message_len_t))
            return;
        memcpy(&msg_len, this->buffer, sizeof(tbus_message_len_t));
        if(msg_len > STATIC_BUFFER_SIZE)
        {
            uint8_t* new_buffer = realloc(this->buffer, msg_len);
            if(!new_buffer)
            {
                /** Another option is to read out and ignore this packet */
                error_handler(this);
                return;
            }
            this->buffer = new_buffer;
            this->buffer_size = msg_len;
        }
    }
    memcpy(&msg_len, this->buffer, sizeof(tbus_message_len_t));
    /** read rest of the data */
    ssize_t read_len = read(
        this->fd,
        this->buffer + this->buffer_offset,
        msg_len - this->buffer_offset);
    switch(read_len)
    {
        case -1:
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            /** Error */
            error_handler(this);
            return;
        case 0:
            /** EOF */
            error_handler(this);
            return;
        default:
            break;
    }
    this->buffer_offset += read_len;
    if(this->buffer_offset < msg_len)
        return;
    /** We have a full message */
    tbus_message_t msg;
    if(tbus_message_view(this->buffer, msg_len, &msg) != 0)
    {
        /** ignore this message */
        goto finish;
    }
    if(this->iface.callbacks.on_message)
    {
        this->iface.callbacks.on_message(&msg, this->iface.callbacks.on_message_ctx);
    }
finish:
    this->buffer_offset = 0;
    if(this->buffer_size > STATIC_BUFFER_SIZE)
    {
        this->buffer = realloc(this->buffer, STATIC_BUFFER_SIZE);
        this->buffer_size = STATIC_BUFFER_SIZE;
    }
}

static void error_handler(message_reader_impl_t* this)
{
    if(!this->iface.callbacks.on_error)
    {
        /** Critical, abort */
        assert("No error handler" == NULL);
    }
    this->iface.callbacks.on_error(this->iface.callbacks.on_error_ctx);
}
