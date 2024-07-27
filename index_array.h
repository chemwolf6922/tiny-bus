#pragma once

#include <stdint.h>
#include <stddef.h>

typedef uint64_t index_array_key_t;

typedef struct index_array_s index_array_t;
struct index_array_s
{
    void (*destroy)(index_array_t* self, void (*clear_data)(void* data, void* ctx), void* ctx);
    void* (*alloc)(index_array_t* self, index_array_key_t* key);
    void (*free)(index_array_t* self, index_array_key_t index, void* old_data);
    void* (*get)(index_array_t* self, index_array_key_t index);
};

index_array_t* index_array_new(size_t data_size);

#define INDEX_ARRAY_NEW(type) index_array_new(sizeof(type))
