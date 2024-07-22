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

static tbus_broker_t* broker = NULL;

int main(int argc, char const *argv[])
{
    char* uds_path = TBUS_DEFAULT_UDS_PATH;
    int opt;
    while((opt = getopt(argc, argv, "p:")) != -1)
    {
        switch(opt)
        {
            case 'p':
                uds_path = optarg;
                break;
            default:
                break;
        }
    }
    if(!uds_path)
        _exit(EXIT_FAILURE);
    if(broker_init(uds_path) < 0)
        _exit(EXIT_FAILURE);
    tev_main_loop(broker->tev);
    broker_deinit();
    return 0;
}

static int broker_init(const char* uds_path)
{
    if(broker)
        return -1;
    if(!uds_path)
        goto error;
    broker = malloc(sizeof(tbus_broker_t));
    if(!broker)
        goto error;
    bzero(broker, sizeof(tbus_broker_t));
    LIST_INIT(&broker->clients);
    LIST_INIT(&broker->buffers);
    broker->topics = topic_tree_new();
    if(!broker->topics)
        goto error;
    broker->tev = tev_create_ctx();
    if(!broker->tev)
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
    if(broker->tev)
        tev_free_ctx(broker->tev);
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

static void handle_publish(const tbus_message_t* msg, tbus_client_t* client)
{

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
