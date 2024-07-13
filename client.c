#include <tev/map.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "tbus.h"
#include "message.h"
#include "socket_util.h"

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
    stream_reader_t* reader;
    /** Map<string, client_subscription_t*> */
    map_handle_t subscriptions_by_topic;
    /** Map<tbus_message_sub_index_t, client_subscription&> */
    map_handle_t subscriptions_by_index;
    tbus_message_sub_index_t next_index;
} tbus_client_t;

static void client_close(tbus_t* iface);
static int client_subscribe(tbus_t* iface, const char* topic, tbus_subscribe_callback_t callback, void* ctx);
static void client_unsubscribe(tbus_t* iface, const char* topic);
static int client_publish(tbus_t* iface, const char* topic, const uint8_t* data, uint32_t len);
static void client_read_handler(void* ctx);
static inline void handle_pub_message(tbus_client_t* client, const tbus_message_t* msg);
static int write_message(int fd, const tbus_message_t* msg);
static void free_subscription(client_subscription_t* subscription);
static void free_subscription_with_ctx(void* data, void* ctx);

tbus_t* tbus_connect(tev_handle_t tev, const char* uds_path)
{
    tbus_client_t* client = malloc(sizeof(tbus_client_t));
    if (client == NULL)
        goto error;
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
    client->reader = stream_reader_new();
    if (client->reader == NULL)
        goto error;
    client->fd = uds_connect(uds_path);
    if (client->fd < 0)
        goto error;
    if(tev_set_read_handler(tev, client->fd, client_read_handler, client) != 0)
        goto error;
    return &client->iface;
error:
    client_close((tbus_t*)client);
    return NULL;
}

static void client_close(tbus_t* iface)
{
    tbus_client_t* client = (tbus_client_t*)iface;
    if(client == NULL)
        return;
    if(client->fd >= 0)
    {
        tev_set_read_handler(client->tev, client->fd, NULL, NULL);
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
    if(client->reader != NULL)
    {
        client->reader->free(client->reader);
    }
    free(client);
}

static int client_subscribe(tbus_t* iface, const char* topic, tbus_subscribe_callback_t callback, void* ctx)
{
    if(iface == NULL || topic == NULL || callback == NULL)
        return -1;
    tbus_client_t* client = (tbus_client_t*)iface;
    client_subscription_t* subscription = map_get(client->subscriptions_by_topic, (void*)topic, strlen(topic));
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
    subscription->index = client->next_index++;
    subscription->callback = callback;
    subscription->ctx = ctx;
    if(map_add(client->subscriptions_by_topic, (void*)topic, strlen(topic), subscription) == NULL)
        goto error;
    if(map_add(client->subscriptions_by_index, &subscription->index, sizeof(subscription->index), subscription) == NULL)
        goto error;
    /** send subscription message */
    tbus_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = TBUS_MSG_CMD_SUB;
    msg.topic = (char*)topic;
    msg.p_sub_index = &subscription->index;
    if(write_message(client->fd, &msg) != 0)
        goto error;
    return 0;
error:
    if(subscription != NULL)
    {
        map_remove(client->subscriptions_by_topic, (void*)topic, strlen(topic));
        map_remove(client->subscriptions_by_index, &subscription->index, sizeof(subscription->index));
        free_subscription(subscription);
    }
    return -1;
}

static void client_unsubscribe(tbus_t* iface, const char* topic)
{
    if(iface == NULL || topic == NULL)
        return;
    tbus_client_t* client = (tbus_client_t*)iface;
    client_subscription_t* subscription = map_remove(client->subscriptions_by_topic, (void*)topic, strlen(topic));
    if(subscription == NULL)
        return;
    map_remove(client->subscriptions_by_index, &subscription->index, sizeof(subscription->index));
    free_subscription(subscription);
    /** send unsubscribe message */
    tbus_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = TBUS_MSG_CMD_UNSUB;
    msg.topic = (char*)topic;
    write_message(client->fd, &msg);  
}

static int client_publish(tbus_t* iface, const char* topic, const uint8_t* data, uint32_t len)
{
    if(iface == NULL || topic == NULL || data == NULL || len == 0)
        return -1;
    tbus_client_t* client = (tbus_client_t*)iface;
    tbus_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.command = TBUS_MSG_CMD_PUB;
    msg.topic = (char*)topic;
    msg.data = (uint8_t*)data;
    msg.len = len;
    return write_message(client->fd, &msg);
}

static void client_read_handler(void* ctx)
{
    tbus_client_t* client = (tbus_client_t*)ctx;
    uint8_t* buffer = NULL;
    ssize_t len = client->reader->read(client->reader, client->fd, &buffer);
    /** Note len is pre processed and is different from read's return */
    switch (len)
    {
        case -1:
            // Error or disconnect
            void (*on_disconnect)(void*) = client->iface.callbacks.on_disconnect;
            void* on_disconnect_ctx = client->iface.callbacks.on_disconnect_ctx;
            client_close((tbus_t*)client);
            if(on_disconnect != NULL)
            {
                on_disconnect(on_disconnect_ctx);
            }
            else
            {
                /** Critical error, abort */
                assert("Tbus client disconnected without on_disconnect callback" == NULL);
            }
            break;
        case 0:
            // EAGAIN or EWOULDBLOCK, ignore
            break;
        default:
            // Process message
            tbus_message_t msg;
            if(tbus_message_view(buffer, len, &msg) != 0)
            {
                // Invalid message, ignore
                break;
            }
            switch (msg.command)
            {
                case TBUS_MSG_CMD_PUB:
                    handle_pub_message(client, &msg);
                    break;
                default:
                    // Invalid message, ignore
                    break;
            }
            break;
    }
    if(buffer != NULL)
    {
        client->reader->handoff_buffer(client->reader, buffer);
    }
}

static inline void handle_pub_message(tbus_client_t* client, const tbus_message_t* msg)
{
    if(msg->p_sub_index == NULL)
    {
        // Invalid message, ignore
        return;
    }
    client_subscription_t* subscription = map_get(
        client->subscriptions_by_index, 
        msg->p_sub_index, 
        sizeof(*msg->p_sub_index));
    if(subscription == NULL)
    {
        // Invalid subscription, ignore
        return;
    }
    subscription->callback(msg->topic, msg->data, msg->len, subscription->ctx);
}

static int write_message(int fd, const tbus_message_t* msg)
{
    int len = 0;
    uint8_t* buffer = tbus_message_serialize(msg, &len);
    if(buffer == NULL)
        return -1;
    ssize_t written = write(fd, buffer, len);
    free(buffer);
    return written == len ? 0 : -1;
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
