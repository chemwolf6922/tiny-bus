#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include "message_reader.h"

// Fit the reader in one page
#define STATIC_BUFFER_SIZE (3840)

typedef struct
{
    message_reader_t iface;
    tev_handle_t tev;   
    int fd;
    uint8_t static_buffer[STATIC_BUFFER_SIZE];
    uint8_t* dynamic_buffer;
    size_t dynamic_buffer_size;
    size_t buffer_offset;
} message_reader_impl_t;

static void message_reader_close(message_reader_t* self);
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
    this->tev = tev;
    this->fd = fd;
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
    if(this->dynamic_buffer)
        free(this->dynamic_buffer);
    free(this);
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
            this->static_buffer + this->buffer_offset, 
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
        memcpy(&msg_len, this->static_buffer, sizeof(tbus_message_len_t));
        if(msg_len > STATIC_BUFFER_SIZE)
        {
            this->dynamic_buffer = malloc(msg_len);
            if(!this->dynamic_buffer)
            {
                /** Another option is to read out and ignore this packet */
                error_handler(this);
                return;
            }
            memcpy(this->dynamic_buffer, this->static_buffer, sizeof(tbus_message_len_t));
            this->dynamic_buffer_size = msg_len;
        }
    }
    memcpy(&msg_len, this->static_buffer, sizeof(tbus_message_len_t));
    /** read rest of the data */
    uint8_t* buffer = this->dynamic_buffer ? this->dynamic_buffer : this->static_buffer;
    ssize_t read_len = read(
        this->fd,
        buffer + this->buffer_offset,
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
    if(tbus_message_view(buffer, msg_len, &msg) != 0)
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
    if(this->dynamic_buffer)
    {
        free(this->dynamic_buffer);
        this->dynamic_buffer = NULL;
        this->dynamic_buffer_size = 0;
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
