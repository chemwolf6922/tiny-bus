#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <tev/tev.h>
#include <tev/map.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include "message.h"
#include "message_reader.h"
#include "topic_tree.h"
#include "list.h"
#include "common.h"

/**
 * The tbus broker
 */

#define LISTEN_BACKLOG (10)

typedef struct tbus_subscription_s tbus_subscription_t;
typedef struct tbus_client_s tbus_client_t;

typedef struct
{
    list_head_t node;
    int ref_count;
    uint8_t* data;
    size_t size;
} tbus_buffer_t;

#define GET_BUFFER_FROM_NODE(node) \
    ((tbus_buffer_t*)((char*)(node) - offsetof(tbus_buffer_t, node)))

typedef struct
{
    list_head_t node;
    tbus_buffer_t* buffer;
    size_t bytes_written;
    tbus_message_sub_index_t sub_index;
} tbus_buffer_ref_t;

#define GET_BUFFER_REF_FROM_NODE(node) \
    ((tbus_buffer_ref_t*)((char*)(node) - offsetof(tbus_buffer_ref_t, node)))

struct tbus_subscription_s
{
    list_head_t topic_tree_node;
    char* topic;
    tbus_message_sub_index_t sub_index;
    tbus_client_t* client;
};

#define GET_SUBSCRIPTION_FROM_TOPIC_TREE_NODE(node) \
    ((tbus_subscription_t*)((char*)(node) - offsetof(tbus_subscription_t, topic_tree_node)))

struct tbus_client_s
{
    list_head_t broker_node;
    int fd;
    message_reader_t* reader;
    /** Map<topic, tbus_subscription_t*> */
    map_handle_t subscriptions;
    /** List<tbus_buffer_ref_t> */
    list_head_t buffers;
};

#define GET_CLIENT_FROM_BROKER_NODE(node) \
    ((tbus_client_t*)((char*)(node) - offsetof(tbus_client_t, broker_node)))

typedef struct
{
    tev_handle_t tev;
    int fd;
    /** TopicTree<List<tbus_subscription_t&>*> */
    topic_tree_t* topics;
    /** List<tbus_client_t> */
    list_head_t clients;
    /** List<tbus_buffer_t> */
    list_head_t buffers;
} tbus_broker_t;

static int broker_init(tev_handle_t tev, const char* uds_path);
static void broker_deinit();
static int uds_listen(const char* path);
static void on_client_connect(void* ctx);
static tbus_client_t* tbus_client_new(tev_handle_t tev, int fd);
static void tbus_client_free(tbus_client_t* client);
static void on_client_message(const tbus_message_t* msg, void* ctx);
static void handle_subscription(const tbus_message_t* msg, tbus_client_t* client);
static void handle_unsubscription(const tbus_message_t* msg, tbus_client_t* client);
static void handle_publish(const tbus_message_t* msg, tbus_client_t* client);
static void publish_on_match(void* data, void* ctx);
static void on_client_write_ready(void* ctx);
static void on_client_error(void* ctx);
static tbus_subscription_t* tbus_subscription_new(const char* topic, tbus_message_sub_index_t* p_sub_index, tbus_client_t* client);
static void tbus_subscription_free(tbus_subscription_t* sub);
static tbus_buffer_t* tbus_buffer_new(uint8_t* data, size_t size);
static void tbus_buffer_free(tbus_buffer_t* buffer);
static tbus_buffer_ref_t* tbus_buffer_ref_new(tbus_buffer_t* buffer);
static void tbus_buffer_ref_free(tbus_buffer_ref_t* ref);
static void free_list_head_with_ctx(void* data, void* ctx);

#ifdef USE_SIGNAL
#include <sys/eventfd.h>
#include <signal.h>
static void signal_handler(int signal);
static void signal_event_fd_read_handler(void* ctx);
static int signal_event_fd = -1;
#endif
static tbus_broker_t* broker = NULL;

int main(int argc, char const *argv[])
{
    int rc = 0;
    /** parse args */
    char* uds_path = TBUS_DEFAULT_UDS_PATH;
    int opt;
    while((opt = getopt(argc, (char**)argv, "p:v")) != -1)
    {
        switch(opt)
        {
            case 'p':
                uds_path = optarg;
                break;
            case 'v':
                printf("Tbus broker version: %s\n", TBUS_VERSION);
                exit(EXIT_SUCCESS);
                break;
            default:
                break;
        }
    }
    if(!uds_path)
        exit(EXIT_FAILURE);
    /** init */
    tev_handle_t tev = tev_create_ctx();
    if(!tev)
    {
        fprintf(stderr, "Failed to create tev context\n");
        exit(EXIT_FAILURE);
    }
#ifdef USE_SIGNAL
    signal_event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if(signal_event_fd == -1)
    {
        fprintf(stderr, "Failed to create eventfd\n");
        exit(EXIT_FAILURE);
    }
    rc = tev_set_read_handler(tev, signal_event_fd, signal_event_fd_read_handler, NULL);
    if(rc != 0)
    {
        fprintf(stderr, "Failed to set read handler for signal event fd\n");
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif
    rc = broker_init(tev, uds_path);
    if(rc != 0)
    {
        fprintf(stderr, "Failed to init broker\n");
        exit(EXIT_FAILURE);
    }
    /** event loop */
    tev_main_loop(tev);
    /** deinit */
    broker_deinit();
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
    tev_handle_t tev = broker->tev;
    broker_deinit();
    tev_set_read_handler(tev, signal_event_fd, NULL, NULL);
    close(signal_event_fd);
    signal_event_fd = -1;
}
#endif

static int broker_init(tev_handle_t tev, const char* uds_path)
{
    if(broker)
        return -1;
    if(!uds_path || !tev)
        goto error;
    broker = malloc(sizeof(tbus_broker_t));
    if(!broker)
        goto error;
    bzero(broker, sizeof(tbus_broker_t));
    broker->tev = tev;
    LIST_INIT(&broker->clients);
    LIST_INIT(&broker->buffers);
    broker->topics = topic_tree_new();
    if(!broker->topics)
        goto error;
    broker->fd = uds_listen(uds_path);
    if(broker->fd < 0)
        goto error;
    if(tev_set_read_handler(broker->tev, broker->fd, on_client_connect, NULL) < 0)
        goto error;
    return 0;
error:
    broker_deinit();
    return -1;
}

static void broker_deinit()
{
    if(!broker)
        return;
    LIST_FOR_EACH_SAFE(&broker->clients, node)
    {
        tbus_client_t* client = GET_CLIENT_FROM_BROKER_NODE(node);
        tbus_client_free(client);
    }
    LIST_FOR_EACH_SAFE(&broker->buffers, node)
    {
        tbus_buffer_t* buffer = GET_BUFFER_FROM_NODE(node);
        tbus_buffer_free(buffer);
    }
    if(broker->fd >= 0)
    {
        tev_set_read_handler(broker->tev, broker->fd, NULL, NULL);
        close(broker->fd);
    }
    if(broker->topics)
        broker->topics->free(broker->topics, free_list_head_with_ctx, NULL);
    free(broker);
    broker = NULL;
}

static int uds_listen(const char* path)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
    if(path[0] == '@')
        addr.sun_path[0] = 0;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if(fd < 0)
        return -1;
    if(bind(fd, (struct sockaddr*)&addr, addr_len) != 0)
    {
        close(fd);
        return -1;
    }
    if(listen(fd, LISTEN_BACKLOG) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static void on_client_connect(void* ctx)
{
    int fd = -1;
    tbus_client_t* client = NULL;

    struct sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);
    fd = accept(broker->fd, (struct sockaddr*)&addr, &addr_len);
    if(fd < 0)
        goto error;
    client = tbus_client_new(broker->tev, fd);
    if(!client)
        goto error;
    LIST_LINK(&broker->clients, &client->broker_node);
    return;
error:
    if(client)
        tbus_client_free(client);
    if(fd >= 0)
        close(fd);
}

static tbus_client_t* tbus_client_new(tev_handle_t tev, int fd)
{
    tbus_client_t* client = malloc(sizeof(tbus_client_t));
    if(!client)
        goto error;
    bzero(client, sizeof(tbus_client_t));
    client->subscriptions = map_create();
    if(!client->subscriptions)
        goto error;
    LIST_INIT(&client->buffers);
    client->fd = fd;
    client->reader = message_reader_new(tev, fd);
    if(!client->reader)
        goto error;
    client->reader->callbacks.on_message = on_client_message;
    client->reader->callbacks.on_message_ctx = client;
    client->reader->callbacks.on_error = on_client_error;
    client->reader->callbacks.on_error_ctx = client;
    return client;
error:
    if(client)
        tbus_client_free(client);
    return NULL;
}

static void tbus_client_free(tbus_client_t* client)
{
    if(!client)
        return;
    if(client->reader)
        client->reader->close(client->reader);
    if(client->fd >= 0)
        close(client->fd);
    if(client->subscriptions)
    {
        map_entry_t entry = {0};
        map_forEach(client->subscriptions, entry)
        {
            tbus_subscription_t* sub = entry.value;
            list_head_t* topic_tree_entry_data = sub->topic_tree_node.next;
            LIST_UNLINK(&sub->topic_tree_node);
            if(LIST_IS_EMPTY(topic_tree_entry_data))
            {
                broker->topics->remove(broker->topics, sub->topic);
                free(topic_tree_entry_data);
            }
            tbus_subscription_free(sub);
        }
        map_delete(client->subscriptions, NULL, NULL);
    }
    LIST_FOR_EACH_SAFE(&client->buffers, node)
    {
        tbus_buffer_ref_t* ref = GET_BUFFER_REF_FROM_NODE(node);
        ref->buffer->ref_count --;
        if(ref->buffer->ref_count == 0)
        {
            LIST_UNLINK(&ref->buffer->node);
            tbus_buffer_free(ref->buffer);
        }
        tbus_buffer_ref_free(ref);
    }
    free(client);
}

static void on_client_message(const tbus_message_t* msg, void* ctx)
{
    tbus_client_t* client = (tbus_client_t*)ctx;
    if(!msg || !client)
        return;
    switch(msg->command)
    {
        case TBUS_MSG_CMD_SUB:
            handle_subscription(msg, client);
            break;
        case TBUS_MSG_CMD_UNSUB:
            handle_unsubscription(msg, client);
            break;
        case TBUS_MSG_CMD_PUB:
            handle_publish(msg, client);
            break;
        default:
            break;
    }
}

static void handle_subscription(const tbus_message_t* msg, tbus_client_t* client)
{
    /** check parameters */
    if(!msg->topic || !msg->p_sub_index)
        return;
    tbus_subscription_t* sub = map_get(client->subscriptions, msg->topic, strlen(msg->topic));
    if(sub)
    {
        /** update sub index for existing subscription */
        READ_SUB_INDEX(msg, sub->sub_index);
        return;
    }
    sub = tbus_subscription_new(msg->topic, msg->p_sub_index, client);
    if(!sub)
        return;
    if(!map_add(client->subscriptions, sub->topic, strlen(sub->topic), sub))
    {
        tbus_subscription_free(sub);
        return;
    }
    list_head_t* topic_tree_entry = broker->topics->get(broker->topics, sub->topic);
    if(!topic_tree_entry)
    {
        topic_tree_entry = malloc(sizeof(list_head_t));
        if(!topic_tree_entry)
        {
            map_remove(client->subscriptions, sub->topic, strlen(sub->topic));
            tbus_subscription_free(sub);
            return;
        }
        LIST_INIT(topic_tree_entry);
        if(!broker->topics->insert(broker->topics, sub->topic, topic_tree_entry))
        {
            free(topic_tree_entry);
            map_remove(client->subscriptions, sub->topic, strlen(sub->topic));
            tbus_subscription_free(sub);
            return;
        }
    }
    LIST_LINK(topic_tree_entry, &sub->topic_tree_node);
}

static void handle_unsubscription(const tbus_message_t* msg, tbus_client_t* client)
{
    /** check parameters */
    if(!msg->topic)
        return;
    tbus_subscription_t* sub = map_remove(client->subscriptions, msg->topic, strlen(msg->topic));
    if(!sub)
        return;
    list_head_t* topic_tree_entry = sub->topic_tree_node.next;
    LIST_UNLINK(&sub->topic_tree_node);
    if(LIST_IS_EMPTY(topic_tree_entry))
    {
        broker->topics->remove(broker->topics, sub->topic);
        free(topic_tree_entry);
    }
    tbus_subscription_free(sub);
}

typedef struct
{
    tbus_buffer_t* buffer;
    tbus_message_t* view;
    list_head_t error_clients;
} publish_on_match_ctx_t;

static void handle_publish(const tbus_message_t* msg, tbus_client_t* client)
{
    /** Check parameters. */
    if(!msg->topic || !msg->p_sub_index || !msg->data || msg->data_len == 0)
        return;
    size_t raw_buffer_size = 0;
    uint8_t* raw_buffer = client->reader->get_buffer(client->reader, &raw_buffer_size);
    tbus_buffer_t* buffer = tbus_buffer_new(raw_buffer, raw_buffer_size);
    if(!buffer)
        return;
    publish_on_match_ctx_t ctx = {
        .buffer = buffer,
        .view = (tbus_message_t*)msg
    };
    LIST_INIT(&ctx.error_clients);
    broker->topics->match(broker->topics, msg->topic, publish_on_match, &ctx);
    if(ctx.buffer->ref_count == 0)
    {
        /** All first transmission finished */
        /** Unref data */
        ctx.buffer->data = NULL;
        tbus_buffer_free(ctx.buffer);
        return;
    }
    /** Acquire the buffer and store in buffers */
    uint8_t* take_over_buffer = client->reader->take_over_buffer(client->reader, NULL);
    /** Critical */
    if(!take_over_buffer)
    {
        fprintf(stderr, "Critical error: Failed to take over buffer\n");
        exit(EXIT_FAILURE);
    }
    LIST_LINK(&broker->buffers, &ctx.buffer->node);
    /** Close error clients. Do it here to avoid client being one of them. */
    LIST_FOR_EACH_SAFE(&ctx.error_clients, node)
    {
        tbus_client_t* error_client = GET_CLIENT_FROM_BROKER_NODE(node);
        tbus_client_free(error_client);
    }
}

static void publish_on_match_handle_subscription(tbus_subscription_t* sub, publish_on_match_ctx_t* publish_ctx);

static void publish_on_match(void* data, void* ctx)
{
    list_head_t* subs = (list_head_t*)data;
    publish_on_match_ctx_t* publish_ctx = (publish_on_match_ctx_t*)ctx;
    /** Overwrite the sub_index */
    LIST_FOR_EACH_SAFE(subs, node)
    {
        tbus_subscription_t* sub = GET_SUBSCRIPTION_FROM_TOPIC_TREE_NODE(node);
        publish_on_match_handle_subscription(sub, publish_ctx);
    }
}

static void publish_on_match_handle_subscription(tbus_subscription_t* sub, publish_on_match_ctx_t* publish_ctx)
{
    ssize_t bytes_written = 0;
    /** Check if client is already in error list, this list should be short. */
    LIST_FOR_EACH(&publish_ctx->error_clients, node)
    {
        tbus_client_t* error_client = GET_CLIENT_FROM_BROKER_NODE(node);
        if(error_client == sub->client)
            return;
    }
    /** Overwrite sub index. */
    WRITE_SUB_INDEX(publish_ctx->view, sub->sub_index);
    /** Try write message in one go */
    if(!LIST_IS_EMPTY(&sub->client->buffers))
    {
        /** Client is busy */
        goto add_ref;
    }
    bytes_written = send(sub->client->fd, publish_ctx->buffer->data, publish_ctx->buffer->size, MSG_NOSIGNAL);
    if(bytes_written < 0)
    {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            /** Client is busy */
            goto add_ref;
        }
        /** Client error */
        LIST_UNLINK(&sub->client->broker_node);
        LIST_LINK(&publish_ctx->error_clients, &sub->client->broker_node);
        return;
    }
    if(bytes_written == publish_ctx->buffer->size)
        return;
add_ref:
    tbus_buffer_ref_t* ref = tbus_buffer_ref_new(publish_ctx->buffer);
    if(!ref)
    {
        /** Critical for that client */
        LIST_UNLINK(&sub->client->broker_node);
        LIST_LINK(&publish_ctx->error_clients, &sub->client->broker_node);
        return;
    }
    ref->bytes_written = bytes_written;
    ref->sub_index = sub->sub_index;
    LIST_LINK(&sub->client->buffers, &ref->node);
    publish_ctx->buffer->ref_count ++;
    tev_set_write_handler(broker->tev, sub->client->fd, on_client_write_ready, sub->client);
}

static void on_client_write_ready(void* ctx)
{
    tbus_client_t* client = (tbus_client_t* )ctx;
    LIST_FOR_EACH_SAFE(&client->buffers, node)
    {   
        tbus_buffer_ref_t* ref = GET_BUFFER_REF_FROM_NODE(node);
        ssize_t bytes_written = send(client->fd, ref->buffer->data + ref->bytes_written, ref->buffer->size - ref->bytes_written, MSG_NOSIGNAL);
        if(bytes_written < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                /** Client is busy */
                break;
            }
            /** Client error */
            LIST_UNLINK(&client->broker_node);
            tbus_buffer_ref_free(ref);
            return;
        }
        ref->bytes_written += bytes_written;
        if(ref->bytes_written == ref->buffer->size)
        {
            /** Transmission finished */
            LIST_UNLINK(&ref->node);
            ref->buffer->ref_count --;
            if(ref->buffer->ref_count == 0)
            {
                LIST_UNLINK(&ref->buffer->node);
                tbus_buffer_free(ref->buffer);
            }
            tbus_buffer_ref_free(ref);
        }
    }
    if(!LIST_IS_EMPTY(&client->buffers))
    {
        /** Still have data to write. Write handler is still valid */
        return;
    }
    tev_set_write_handler(broker->tev, client->fd, NULL, NULL);
}

static void on_client_error(void* ctx)
{
    tbus_client_t* client = (tbus_client_t*)ctx;
    LIST_UNLINK(&client->broker_node);
    tbus_client_free(client);
}

static tbus_subscription_t* tbus_subscription_new(const char* topic, tbus_message_sub_index_t* p_sub_index, tbus_client_t* client)
{
    if (!topic || !client)
        return NULL;
    tbus_subscription_t* sub = malloc(sizeof(tbus_subscription_t));
    if(!sub)
        goto error;
    bzero(sub, sizeof(tbus_subscription_t));
    sub->topic = strdup(topic);
    if(!sub->topic)
        goto error;
    if (p_sub_index)
        memcpy(&sub->sub_index, p_sub_index, sizeof(tbus_message_sub_index_t));
    sub->client = client;
    return sub;
error:
    if(sub)
        tbus_subscription_free(sub);
    return NULL;
}

static void tbus_subscription_free(tbus_subscription_t* sub)
{
    if(!sub)
        return;
    if(sub->topic)
        free(sub->topic);
    free(sub);
}

static tbus_buffer_t* tbus_buffer_new(uint8_t* data, size_t size)
{
    tbus_buffer_t* buffer = malloc(sizeof(tbus_buffer_t));
    if(!buffer)
        return NULL;
    bzero(buffer, sizeof(tbus_buffer_t));
    buffer->data = data;
    buffer->size = size;
    return buffer;
}

static void tbus_buffer_free(tbus_buffer_t* buffer)
{
    if(!buffer)
        return;
    if(buffer->data)
        free(buffer->data);
    free(buffer);
}

static tbus_buffer_ref_t* tbus_buffer_ref_new(tbus_buffer_t* buffer)
{
    tbus_buffer_ref_t* ref = malloc(sizeof(tbus_buffer_ref_t));
    if(!ref)
        return NULL;
    bzero(ref, sizeof(tbus_buffer_ref_t));
    ref->buffer = buffer;
    return ref;
}

static void tbus_buffer_ref_free(tbus_buffer_ref_t* ref)
{
    if(!ref)
        return;
    free(ref);
}

static void free_list_head_with_ctx(void* data, void* ctx)
{
    if(data)
        free(data);
}
