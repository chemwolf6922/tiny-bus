#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "message_writer.h"
#include "message.h"
#include "list.h"

typedef struct
{
    list_head_t node;
    uint8_t* buffer;
    size_t size;
    size_t bytes_written;
} message_buffer_t;

#define GET_MESSAGE_BUFFER_FROM_NODE(node) \
    ((message_buffer_t*)((char*)(node) - offsetof(message_buffer_t, node)))

typedef struct
{
    message_writer_t iface;
    tev_handle_t tev;   
    int fd;
    list_head_t buffers;
} message_writer_impl_t;

static void message_writer_close(message_writer_t* iface);
static int message_writer_write_message(message_writer_t* iface, const tbus_message_t* msg);
static void write_handler(void* ctx);
static void error_handler(message_writer_impl_t* this);
static message_buffer_t* message_buffer_new(const tbus_message_t* msg);
static void message_buffer_free(message_buffer_t* this);

message_writer_t* message_writer_new(tev_handle_t tev, int fd)
{
    message_writer_impl_t* self = malloc(sizeof(message_writer_impl_t));
    self->iface.close = message_writer_close;
    self->iface.write_message = message_writer_write_message;
    self->tev = tev;
    self->fd = fd;
    LIST_INIT(&self->buffers);
    return (message_writer_t*)self;
error:
    message_writer_close((message_writer_t*)self);
    return NULL;
}

static void message_writer_close(message_writer_t* iface)
{
    message_writer_impl_t* this = (message_writer_impl_t*)iface;
    if(!this)
    {
        return;
    }
    LIST_FOR_EACH_SAFE(&this->buffers, node)
    {
        message_buffer_t* buffer = GET_MESSAGE_BUFFER_FROM_NODE(node);
        message_buffer_free(buffer);
    }
    if(this->tev && this->fd >=0)
        tev_set_write_handler(this->tev, this->fd, NULL, NULL);
    free(this);
}

static int message_writer_write_message(message_writer_t* iface, const tbus_message_t* msg)
{
    message_writer_impl_t* this = (message_writer_impl_t*)iface;
    if(!this || !msg)
    {
        return -1;
    }
    message_buffer_t* buffer = message_buffer_new(msg);
    if(!buffer)
    {
        return -1;
    }
    LIST_LINK(&this->buffers, &buffer->node);
    write_handler(this);
    return 0;
}

static void write_handler(void* ctx)
{
    message_writer_impl_t* this = ctx;
    LIST_FOR_EACH_SAFE(&this->buffers, node)
    {
        message_buffer_t* buffer = GET_MESSAGE_BUFFER_FROM_NODE(node);
        ssize_t bytes_written = send(this->fd, buffer->buffer + buffer->bytes_written, buffer->size - buffer->bytes_written, MSG_NOSIGNAL);
        if(bytes_written < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                goto finish;                
            error_handler(this);
            return;
        }
        buffer->bytes_written += bytes_written;
        if(buffer->bytes_written < buffer->size)
            break;
        LIST_UNLINK(node);
        message_buffer_free(buffer);
    }
finish:
    if(LIST_IS_EMPTY(&this->buffers))
        return;
    if(tev_set_write_handler(this->tev, this->fd, write_handler, this) != 0)
        error_handler(this);
}

static void error_handler(message_writer_impl_t* this)
{
    if(!this->iface.callbacks.on_error)
    {
        /** Critical, abort */
        assert("No error handler" == NULL);
    }
    this->iface.callbacks.on_error(this->iface.callbacks.on_error_ctx);
}

static message_buffer_t* message_buffer_new(const tbus_message_t* msg)
{
    message_buffer_t* this = malloc(sizeof(message_buffer_t));
    if(!this)
    {
        goto error;
    }
    memset(this, 0, sizeof(message_buffer_t));
    this->bytes_written = 0;
    this->buffer = tbus_message_serialize(msg, &this->size);
    if(!this->buffer)
    {
        goto error;
    }
    return this;
error:
    message_buffer_free(this);
    return NULL;
}

static void message_buffer_free(message_buffer_t* this)
{
    if(!this)
    {
        return;
    }
    if(this->buffer)
    {
        free(this->buffer);
    }
    free(this);
}

