#include <tev/map.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include "tbus.h"
#include "message.h"
#include "message_reader.h"
#include "message_writer.h"
#include "common.h"

typedef struct
{
    tbus_message_sub_index_t index;    
    tbus_subscribe_callback_t callback;
    void* ctx;
} client_subscription_t;

typedef struct
{
    tbus_t iface;
    tev_handle_t tev;
    int fd;
    message_reader_t* reader;
    message_writer_t* writer;
    /** Map<string, client_subscription_t*> */
    map_handle_t subscriptions_by_topic;
    /** Map<tbus_message_sub_index_t, client_subscription&> */
    map_handle_t subscriptions_by_index;
    tbus_message_sub_index_t next_index;
} tbus_client_t;

static int uds_connect(const char* path);
static void client_close(tbus_t* iface);
static int client_subscribe(tbus_t* iface, const char* topic, tbus_subscribe_callback_t callback, void* ctx);
static void client_unsubscribe(tbus_t* iface, const char* topic);
static int client_publish(tbus_t* iface, const char* topic, const uint8_t* data, uint32_t len);
static void on_message(const tbus_message_t* msg, void* ctx);
static void on_error(void* ctx);
static void free_subscription(client_subscription_t* subscription);
static void free_subscription_with_ctx(void* data, void* ctx);

tbus_t* tbus_connect(tev_handle_t tev, const char* uds_path)
{
    if (!tev)
        return NULL;
    const char* path = uds_path ? uds_path : TBUS_DEFAULT_UDS_PATH;
    tbus_client_t* client = malloc(sizeof(tbus_client_t));
    if (client == NULL)
        goto error;
    memset(client, 0, sizeof(tbus_client_t));
    client->iface.close = client_close;
    client->iface.subscribe = client_subscribe;
    client->iface.unsubscribe = client_unsubscribe;
    client->iface.publish = client_publish;
    client->tev = tev;
    client->subscriptions_by_topic = map_create();
    if (client->subscriptions_by_topic == NULL)
        goto error;
    client->subscriptions_by_index = map_create();
    if (client->subscriptions_by_index == NULL)
        goto error;
    client->next_index = 0;
    client->fd = uds_connect(path);
    if (client->fd < 0)
        goto error;
    client->writer = message_writer_new(tev, client->fd);
    if (client->writer == NULL)
        goto error;
    client->writer->callbacks.on_error = on_error;
    client->writer->callbacks.on_error_ctx = client;
    client->reader = message_reader_new(tev, client->fd);
    if (client->reader == NULL)
        goto error;
    client->reader->callbacks.on_message = on_message;
    client->reader->callbacks.on_message_ctx = client;
    client->reader->callbacks.on_error = on_error;
    client->reader->callbacks.on_error_ctx = client;
    return &client->iface;
error:
    client_close((tbus_t*)client);
    return NULL;
}

static int uds_connect(const char* path)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if(strlen(path) >= sizeof(addr.sun_path) - 1)
        return -1;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
    if(path[0] == '@')
        addr.sun_path[0] = 0;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if(fd < 0)
        return -1;
    if(connect(fd, (struct sockaddr*)&addr, addr_len) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static void client_close(tbus_t* iface)
{
    tbus_client_t* client = (tbus_client_t*)iface;
    if(client == NULL)
        return;
    if(client->reader != NULL)
    {
        client->reader->close(client->reader);
    }
    if(client->writer != NULL)
    {
        client->writer->close(client->writer);
    }
    if(client->fd >= 0)
    {
        close(client->fd);
    }
    if(client->subscriptions_by_index != NULL)
    {
        /** This map only holds a reference */
        map_delete(client->subscriptions_by_index, NULL, NULL);
    }
    if(client->subscriptions_by_topic != NULL)
    {
        map_delete(client->subscriptions_by_topic, free_subscription_with_ctx, NULL);
    }
    free(client);
}

static int client_subscribe(tbus_t* iface, const char* topic, tbus_subscribe_callback_t callback, void* ctx)
{
    if(iface == NULL || topic == NULL || callback == NULL)
        return -1;
    tbus_client_t* this = (tbus_client_t*)iface;
    client_subscription_t* subscription = map_get(this->subscriptions_by_topic, (void*)topic, strlen(topic));
    if(subscription != NULL)
    {
        // Update the subscription
        subscription->callback = callback;
        subscription->ctx = ctx;
        return 0;
    }
    subscription = malloc(sizeof(client_subscription_t));
    if(subscription == NULL)
        goto error;
    memset(subscription, 0, sizeof(client_subscription_t));
    subscription->index = this->next_index++;
    subscription->callback = callback;
    subscription->ctx = ctx;
    if(map_add(this->subscriptions_by_topic, (void*)topic, strlen(topic), subscription) == NULL)
        goto error;
    if(map_add(this->subscriptions_by_index, &subscription->index, sizeof(subscription->index), subscription) == NULL)
        goto error;
    /** send subscription message */
    tbus_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = TBUS_MSG_CMD_SUB;
    msg.topic = (char*)topic;
    msg.p_sub_index = &subscription->index;
    if(this->writer->write_message(this->writer, &msg) != 0)
        goto error;
    return 0;
error:
    if(subscription != NULL)
    {
        map_remove(this->subscriptions_by_topic, (void*)topic, strlen(topic));
        map_remove(this->subscriptions_by_index, &subscription->index, sizeof(subscription->index));
        free_subscription(subscription);
    }
    return -1;
}

static void client_unsubscribe(tbus_t* iface, const char* topic)
{
    if(iface == NULL || topic == NULL)
        return;
    tbus_client_t* this = (tbus_client_t*)iface;
    client_subscription_t* subscription = map_remove(this->subscriptions_by_topic, (void*)topic, strlen(topic));
    if(subscription == NULL)
        return;
    map_remove(this->subscriptions_by_index, &subscription->index, sizeof(subscription->index));
    free_subscription(subscription);
    /** send unsubscribe message */
    tbus_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = TBUS_MSG_CMD_UNSUB;
    msg.topic = (char*)topic;
    this->writer->write_message(this->writer, &msg); 
}

static int client_publish(tbus_t* iface, const char* topic, const uint8_t* data, uint32_t len)
{
    if(iface == NULL || topic == NULL || data == NULL || len == 0)
        return -1;
    tbus_client_t* this = (tbus_client_t*)iface;
    tbus_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = TBUS_MSG_CMD_PUB;
    msg.topic = (char*)topic;
    msg.data = (uint8_t*)data;
    msg.data_len = len;
    return this->writer->write_message(this->writer, &msg);
}

static void on_message(const tbus_message_t* msg, void* ctx)
{
    if(msg->p_sub_index == NULL)
    {
        // Invalid message, ignore
        return;
    }
    tbus_client_t* client = (tbus_client_t*)ctx;
    client_subscription_t* subscription = map_get(
        client->subscriptions_by_index, 
        msg->p_sub_index, 
        sizeof(*msg->p_sub_index));
    if(subscription == NULL)
    {
        // Invalid subscription, ignore
        return;
    }
    subscription->callback(msg->topic, msg->data, msg->data_len, subscription->ctx);
}

static void on_error(void* ctx)
{
    tbus_client_t* client = (tbus_client_t*)ctx;
    void (*on_disconnect)(void*) = client->iface.callbacks.on_disconnect;
    void* on_disconnect_ctx = client->iface.callbacks.on_disconnect_ctx;
    client_close((tbus_t*)client);
    if(on_disconnect == NULL)
    {
        /** Critical error, abort */
        fprintf(stderr, "Critical error: on_disconnect callback not set.\n");
        exit(EXIT_FAILURE);
    }
    on_disconnect(on_disconnect_ctx);
}

static void free_subscription(client_subscription_t* subscription)
{
    if(subscription == NULL)
        return;
    free(subscription);
}

static void free_subscription_with_ctx(void* data, void* ctx)
{
    free_subscription((client_subscription_t*)data);
}
