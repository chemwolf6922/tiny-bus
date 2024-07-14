#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <tev/map.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "topic_tree.h"

typedef struct
{
    void (*free_data)(void* data, void* ctx);
    void* ctx;
} topic_tree_node_free_data_ctx_t;

typedef struct topic_tree_node_s topic_tree_node_t;
struct topic_tree_node_s
{
    char* topic_segment;
    int topic_segment_len;
    topic_tree_node_t* parent;
    map_handle_t children;
    void* data;
};

typedef struct
{
    topic_tree_t iface;
    topic_tree_node_t root;
} topic_tree_impl_t;

static void topic_tree_free(topic_tree_t* iface, void (*free_data)(void* data, void* ctx), void* ctx);
static void* topic_tree_insert(topic_tree_t* iface, const char* topic, void* data);
static void* topic_tree_remove(topic_tree_t* iface, const char* topic);
static void topic_tree_match(topic_tree_t* iface, const char* topic, void (*callback)(void* data, void* ctx), void* ctx);
static bool is_valid_topic(const char* topic);
static void topic_tree_match_node(topic_tree_node_t* root, const char* topic, void (*callback)(void* data, void* ctx), void* ctx);
static void topic_tree_node_clear(topic_tree_node_t* node, topic_tree_node_free_data_ctx_t* ctx);
static void topic_tree_node_free(topic_tree_node_t* node, topic_tree_node_free_data_ctx_t* ctx);
static void topic_tree_node_free_with_ctx(void* data, void* ctx);

topic_tree_t* topic_tree_new()
{
    topic_tree_impl_t* tree = malloc(sizeof(topic_tree_impl_t));
    if (tree == NULL)
        goto error;
    memset(tree, 0, sizeof(topic_tree_impl_t));
    tree->iface.free = topic_tree_free;
    tree->iface.insert = topic_tree_insert;
    tree->iface.remove = topic_tree_remove;
    tree->iface.match = topic_tree_match;
    tree->root.topic_segment = NULL;
    tree->root.parent = NULL;
    tree->root.children = map_create();
    if (tree->root.children == NULL)
        goto error;
    tree->root.data = NULL;
    return (topic_tree_t*)tree;
error:
    if(tree)
        topic_tree_free((topic_tree_t*)tree, NULL, NULL);
    return NULL;
}

static void topic_tree_free(topic_tree_t* iface, void (*free_data)(void* data, void* ctx), void* ctx)
{
    topic_tree_impl_t* this = (topic_tree_impl_t*)iface;
    if(!this)
        return;
    topic_tree_node_free_data_ctx_t free_data_ctx = {free_data, ctx};
    topic_tree_node_clear(&this->root, &free_data_ctx);
    free(this);
}

static void* topic_tree_insert(topic_tree_t* iface, const char* topic, void* data)
{
    topic_tree_impl_t* this = (topic_tree_impl_t*)iface;
    if(!this || !is_valid_topic(topic) || !data)
        return NULL;
    /** Get to the node */
    topic_tree_node_t* node = &this->root;
    char* topic_segment = (char*)topic;
    while(*topic_segment)
    {
        int topic_segment_len = strchrnul(topic_segment, '/') - topic_segment;
        topic_tree_node_t* child = map_get(node->children, topic_segment, topic_segment_len);
        if(!child)
        {
            child = malloc(sizeof(topic_tree_node_t));
            if(!child)
                return NULL;
            memset(child, 0, sizeof(topic_tree_node_t));
            child->topic_segment = strndup(topic_segment, topic_segment_len);
            if(!child->topic_segment)
            {
                free(child);
                return NULL;
            }
            child->topic_segment_len = topic_segment_len;
            child->parent = node;
            child->children = map_create();
            if(!child->children)
            {
                free(child->topic_segment);
                free(child);
                return NULL;
            }
            map_add(node->children, child->topic_segment, child->topic_segment_len, child);
        }
        node = child;
        topic_segment += topic_segment_len;
        if(*topic_segment)
            topic_segment++;
    }
    /** Set the data */
    void* old_data = node->data;
    node->data = data;
    return old_data ? old_data : data;
}

static void* topic_tree_remove(topic_tree_t* iface, const char* topic)
{
    topic_tree_impl_t* this = (topic_tree_impl_t*)iface;
    if(!this || !is_valid_topic(topic))
        return NULL;
    /** Find the node */
    topic_tree_node_t* node = &this->root;
    char* topic_segment = (char*)topic;
    while(node && *topic_segment)
    {
        int topic_segment_len = strchrnul(topic_segment, '/') - topic_segment;
        node = map_get(node->children, topic_segment, topic_segment_len);
        topic_segment += topic_segment_len;
        if(*topic_segment)
            topic_segment++;
    }
    if(node == &this->root)
        return NULL;
    /** Clear or delete node */
    if(node && node->data)
    {
        void* data = node->data;
        node->data = NULL;
        if(map_get_length(node->children) == 0)
        {
            topic_tree_node_t* parent = node->parent;
            map_remove(parent->children, node->topic_segment, node->topic_segment_len);
            topic_tree_node_free(node, NULL);
        }
        return data;
    }
    return NULL;
}

static void topic_tree_match(topic_tree_t* iface, const char* topic, void (*callback)(void* data, void* ctx), void* ctx)
{
    topic_tree_impl_t* this = (topic_tree_impl_t*)iface;
    if(!this || !is_valid_topic(topic) || !callback)
        return;
    topic_tree_match_node(&this->root, topic, callback, ctx);
}

static bool is_valid_topic(const char* topic)
{
    if(!topic)
        return false;
    char* topic_segment = (char*)topic;
    while(*topic_segment)
    {
        int segment_len = strchrnul(topic_segment, '/') - topic_segment;
        switch (segment_len)
        {
        case 0:
            /** Empty segment is not allowed */
            return false;
            break;
        case 1:
            /** # segment should only be the last segment */
            if(*topic_segment == '#' && *(topic_segment + 1))
                return false;
            break;
        default:
            break;
        }
        topic_segment += segment_len;
        if(*topic_segment)
            topic_segment++;
    }
    return true;
}

static void topic_tree_match_node(topic_tree_node_t* root, const char* topic, void (*callback)(void* data, void* ctx), void* ctx)
{
    /** Reach the end of the topic */
    if(!*topic)
    {
        /** check if root has data */
        if(root->data)
            callback(root->data, ctx);
        /** check if root has a # child */
        topic_tree_node_t* child = map_get(root->children, "#", 1);
        /** The # child should have data, but whatever. */
        if(child && child->data)
            callback(child->data, ctx);
        return;
    }
    int topic_segment_len = strchrnul(topic, '/') - topic;
    topic_tree_node_t* child = map_get(root->children, (char*)topic, topic_segment_len);
    char* next_topic = (char*)topic + topic_segment_len;
    if(*next_topic)
        next_topic++;
    if(child)
        topic_tree_match_node(child, next_topic, callback, ctx);
    child = map_get(root->children, "+", 1);
    if(child)
        topic_tree_match_node(child, next_topic, callback, ctx);
    child = map_get(root->children, "#", 1);
    if(child && child->data)
        callback(child->data, ctx);
}

static void topic_tree_node_clear(topic_tree_node_t* node, topic_tree_node_free_data_ctx_t* ctx)
{
    if(!node)
        return;
    if(node->topic_segment)
    {
        free(node->topic_segment);
        node->topic_segment = NULL;
    }
    if(node->children)
    {
        map_delete(node->children, topic_tree_node_free_with_ctx, ctx);
        node->children = NULL;
    }
    if(node->data && ctx && ctx->free_data)
    {
        ctx->free_data(node->data, ctx->ctx);
        node->data = NULL;
    }
}

static void topic_tree_node_free(topic_tree_node_t* node, topic_tree_node_free_data_ctx_t* ctx)
{
    topic_tree_node_clear(node, ctx);
    free(node);
}

static void topic_tree_node_free_with_ctx(void* data, void* ctx)
{
    topic_tree_node_free((topic_tree_node_t*)data, (topic_tree_node_free_data_ctx_t*)ctx);
}


