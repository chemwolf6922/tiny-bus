#include <stddef.h>
#include <tev/tev.h>
#include <tev/map.h>
#include "message.h"
#include "topic_tree.h"
#include "list.h"

/**
 * The tbus broker
 */

typedef struct tbus_subscription_s tbus_subscription_t;
typedef struct tbus_client_s tbus_client_t;

struct tbus_subscription_s
{
    list_head_t client_node;
    list_head_t topic_tree_node;
    tbus_message_sub_index_t sub_index;
    tbus_client_t* client;
};

#define GET_SUBSCRIPTION_FROM_CLIENT_NODE(node) \
    ((tbus_subscription_t*)((char*)(node) - offsetof(tbus_subscription_t, client_node)))

#define GET_SUBSCRIPTION_FROM_TOPIC_TREE_NODE(node) \
    ((tbus_subscription_t*)((char*)(node) - offsetof(tbus_subscription_t, topic_tree_node)))

struct tbus_client_s
{
    list_head_t broker_node;
    int fd;
    /** List<tbus_subscription_t*> */
    list_head_t subscriptions;
};

#define GET_CLIENT_FROM_BROKER_NODE(node) \
    ((tbus_client_t*)((char*)(node) - offsetof(tbus_client_t, broker_node)))

typedef struct
{
    tev_handle_t tev;
    int fd;
    /** TopicTree<List<tbus_subscription_t&>*> */
    topic_tree_t* topics;
    /** List<tbus_client_t*> */
    list_head_t clients;
} tbus_broker_t;

static tbus_broker_t* this = NULL;

int main(int argc, char const *argv[])
{
    /* code */
    return 0;
}


