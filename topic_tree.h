#pragma once

typedef struct topic_tree_s topic_tree_t;

struct topic_tree_s
{
    /**
     * @brief Free the topic tree
     * @param self the topic tree
     */
    void (*free)(topic_tree_t* self, void (*free_data)(void* data, void* ctx), void* ctx);

    /**
     * @brief Insert a topic into the tree.
     * @param self the topic tree
     * @param topic the topic to insert
     * @param data the data to associate with the topic. DO NOT use NULL.
     * @return the data previously associated with the topic, 
     *         or data if the topic was not previously in the tree,
     *         or NULL if the operation failed.
     */
    void* (*insert)(topic_tree_t* self, const char* topic, void* data);
    
    /**
     * @brief Remove a topic from the tree.
     * @param self the topic tree
     * @param topic the topic to remove
     * @return the data associated with the topic, or NULL if the topic was not in the tree.
     */
    void* (*remove)(topic_tree_t* self, const char* topic);
    
    /**
     * @note DO NOT modify the tree in callback.
     * @param self the topic tree
     * @param topic the topic to match
     * @param callback the callback to call for each match
     * @param ctx the context to pass to the callback
     */
    void (*match)(topic_tree_t* self, const char* topic, void (*callback)(void* data, void* ctx), void* ctx);
};

topic_tree_t* topic_tree_new();
