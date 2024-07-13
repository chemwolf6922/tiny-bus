#pragma once

typedef struct topic_tree_s topic_tree_t;

struct topic_tree_s
{
    void (*free)(topic_tree_t* self);
    void* (*insert)(topic_tree_t* self, const char* topic, void* data);
    void* (*remove)(topic_tree_t* self, const char* topic);
    void** (*match)(topic_tree_t* self, const char* topic);
};

topic_tree_t* topic_tree_new();
