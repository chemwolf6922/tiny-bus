#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "../topic_tree.h"

typedef struct
{
    const char* topic;
    int ref_count;
    int match_count;
} test_data_t;

test_data_t test_data[] = 
{
    {"a/b",0},
    {"a/b/c",0},
    {"a",0},
    {"bcd",0},
    {"a/b/c/d/e/f",0},
    {"a/#",0},
    {"+/b/#"}
};

static void free_data(void* data, void* ctx)
{
    ((test_data_t*)data)->ref_count--;
}

static void callback(void* data, void* ctx)
{
    test_data_t* test_data = (test_data_t*)data;
    printf("match: %s\n", test_data->topic);
    test_data->match_count++;
}

int main(int argc, char const *argv[])
{
    topic_tree_t* tree = topic_tree_new();
    assert(tree);
    for (int i = 0; i < sizeof(test_data)/sizeof(test_data_t); i++)
    {
        test_data_t* data = &test_data[i];
        void* result = tree->insert(tree, data->topic, data);
        assert(result == data);
        data->ref_count++;
    }
    
    tree->match(tree, "a", callback, NULL);
    assert(test_data[0].match_count == 0);
    assert(test_data[1].match_count == 0);
    assert(test_data[2].match_count == 1);
    assert(test_data[3].match_count == 0);
    assert(test_data[4].match_count == 0);
    assert(test_data[5].match_count == 1);
    assert(test_data[6].match_count == 0);
    for (int i = 0; i < sizeof(test_data)/sizeof(test_data_t); i++)
    {
        test_data[i].match_count = 0;
    }

    tree->match(tree, "a/b/c", callback, NULL);
    assert(test_data[0].match_count == 0);
    assert(test_data[1].match_count == 1);
    assert(test_data[2].match_count == 0);
    assert(test_data[3].match_count == 0);
    assert(test_data[4].match_count == 0);
    assert(test_data[5].match_count == 1);
    assert(test_data[6].match_count == 1);

    for (int i = 0; i < sizeof(test_data)/sizeof(test_data_t); i++)
    {
        test_data_t* data = &test_data[i];
        void* result = (int*)tree->remove(tree, data->topic);
        assert(result == data);
        data->ref_count--;
    }
    tree->free(tree, free_data, NULL);
    for (int i = 0; i < sizeof(test_data)/sizeof(test_data_t); i++)
    {
        assert(test_data[i].ref_count == 0);
    }
    return 0;
}

