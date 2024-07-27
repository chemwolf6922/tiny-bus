#include <stdlib.h>
#include <string.h>
#include "index_array.h"

typedef uint32_t segment_bitmap_t;
#define SEGMENT_SIZE (sizeof(segment_bitmap_t) * 8)
#define segment_bitmap_cto(x) __builtin_ctz(~(x))

typedef struct
{
    uint32_t sequence_number;
    uint8_t data[] __attribute__((aligned(8)));
} index_array_slot_t;

typedef struct
{
    segment_bitmap_t bitmap;
    uint8_t* slots;
} index_array_segment_t;

typedef struct
{
    index_array_t iface;
    size_t data_size;
    size_t slot_size;
    uint32_t sequence_number;
    uint32_t length;
    uint32_t segment_count;
    index_array_segment_t* segments;
} index_array_impl_t;

#define GET_SEGMENT_BITMAP(segment, i) ((segment)->bitmap & (1u << (i)))
#define SET_SEGMENT_BITMAP(segment, i) ((segment)->bitmap |= (1u << (i)))
#define CLEAR_SEGMENT_BITMAP(segment, i) ((segment)->bitmap &= ~(1u << (i)))

#define GET_INDEX_FROM_KEY(key) ((uint32_t)((key) & 0xFFFFFFFF))
#define GET_SEQUENCE_FROM_KEY(key) ((uint32_t)((key) >> 32))
#define GET_KEY(index, sequence) ((((uint64_t)(sequence)) << 32) | ((uint64_t)(index)))

#define GET_SLOT_FROM_SEGMENT(array, segment, index) ((index_array_slot_t*)((segment)->slots + (index) * (array)->slot_size))
#define GET_SLOT_FROM_ARRAY(array, index) GET_SLOT_FROM_SEGMENT(array, &(array)->segments[(index) / SEGMENT_SIZE], (index) & (SEGMENT_SIZE - 1))

static void index_array_destroy(index_array_t* iface, void (*clear_data)(void*, void*), void* ctx);
static void* index_array_alloc(index_array_t* iface, index_array_key_t* key);
static void index_array_free(index_array_t* iface, index_array_key_t key, void* old_data);
static void* index_array_get(index_array_t* iface, index_array_key_t key);
static uint32_t get_next_sequence_number(index_array_impl_t* this);

index_array_t* index_array_new(size_t data_size)
{
    index_array_impl_t* this = malloc(sizeof(index_array_impl_t));
    if(!this)
        return NULL;
    memset(this, 0, sizeof(index_array_impl_t));
    this->iface.destroy = index_array_destroy;
    this->iface.alloc = index_array_alloc;
    this->iface.free = index_array_free;
    this->iface.get = index_array_get;
    this->data_size = data_size;
    this->slot_size = (sizeof(index_array_slot_t) + data_size + 7) & ~7;
    this->sequence_number = 0;
    this->length = 0;
    this->segment_count = 1;
    this->segments = malloc(sizeof(index_array_segment_t) * this->segment_count);
    if(!this->segments)
        goto error;
    memset(this->segments, 0, sizeof(index_array_segment_t) * this->segment_count);
    this->segments[0].slots = malloc(this->slot_size * SEGMENT_SIZE);
    if(!this->segments[0].slots)
        goto error;
    for(uint32_t i = 0; i < SEGMENT_SIZE; i++)
    {
        index_array_slot_t* slot = GET_SLOT_FROM_ARRAY(this, i);
        slot->sequence_number = 0;
    }
    return (index_array_t*)this;
error:
    index_array_destroy((index_array_t*)this, NULL, NULL);
    return NULL;
}

static void index_array_destroy(index_array_t* iface, void (*clear_data)(void*, void*), void* ctx)
{
    if(!iface)
        return;
    index_array_impl_t* this = (index_array_impl_t*)iface;
    if(this->segments)
    {
        for(uint32_t i = 0; i < this->segment_count; i++)
        {
            index_array_segment_t* segment = &this->segments[i];
            if(clear_data)
            {
                for(uint32_t j = 0; j < SEGMENT_SIZE; j++)
                {
                    if(segment->bitmap & (1 << j))
                    {
                        index_array_slot_t* slot = GET_SLOT_FROM_SEGMENT(this, segment, j);
                        clear_data(slot->data, ctx);
                    }
                }
            }
            free(segment->slots);
        }
        free(this->segments);
    }
    free(this);
}

static void* index_array_alloc(index_array_t* iface, index_array_key_t* key)
{
    if(!iface)
        return NULL;
    index_array_impl_t* this = (index_array_impl_t*)iface;
    if(this->length == this->segment_count * SEGMENT_SIZE)
    {
        uint32_t new_segment_count = this->segment_count + 1;
        index_array_segment_t* new_segments = realloc(this->segments, sizeof(index_array_segment_t) * new_segment_count);
        if(!new_segments)
            return NULL;
        this->segments = new_segments;
        index_array_segment_t* new_segment = &this->segments[this->segment_count];
        new_segment->bitmap = 0;
        new_segment->slots = malloc(this->slot_size * SEGMENT_SIZE);
        if(!new_segment->slots)
        {
            /** The new_segment is wasted, but will not corrupt the array */
            return NULL;
        }
        for(uint32_t i = 0; i < SEGMENT_SIZE; i++)
        {
            index_array_slot_t* slot = GET_SLOT_FROM_SEGMENT(this, new_segment, i);
            slot->sequence_number = 0;
        }
        this->segment_count = new_segment_count;
    }
    /** Get the first empty slot */
    index_array_slot_t* slot = NULL;
    for(uint32_t i = 0; i < this->segment_count; i++)
    {
        index_array_segment_t* segment = &this->segments[i];
        if(segment->bitmap == 0xFFFFFFFF)
            continue;
        uint32_t j = segment_bitmap_cto(segment->bitmap);
        slot = GET_SLOT_FROM_SEGMENT(this, segment, j);
        SET_SEGMENT_BITMAP(segment, j);
        slot->sequence_number = get_next_sequence_number(this);
        this->length++;
        *key = GET_KEY(i * SEGMENT_SIZE + j, slot->sequence_number);
        return slot->data;
    }
    return NULL;
}

static void index_array_free(index_array_t* iface, index_array_key_t key, void* old_data)
{
    if(!iface)
        return;
    index_array_impl_t* this = (index_array_impl_t*)iface;
    uint32_t index = GET_INDEX_FROM_KEY(key);
    if(index >= this->length)
        return;
    index_array_slot_t* slot = GET_SLOT_FROM_ARRAY(this, index);
    if(slot->sequence_number != GET_SEQUENCE_FROM_KEY(key))
        return;
    slot->sequence_number = 0;
    this->length--;
    CLEAR_SEGMENT_BITMAP(&this->segments[index >> 5], index & 31);
    if(old_data)
        memcpy(old_data, slot->data, this->data_size);
    
    /** remove the last segment if the last two segments are empty */
    if(this->segment_count > 1 && 
        this->segments[this->segment_count - 2].bitmap == 0 &&
        this->segments[this->segment_count - 1].bitmap == 0)
    {
        free(this->segments[this->segment_count - 1].slots);
        this->segment_count--;
        this->segments = realloc(this->segments, sizeof(index_array_segment_t) * this->segment_count);
    }
}

static void* index_array_get(index_array_t* iface, index_array_key_t key)
{
    if(!iface)
        return NULL;
    index_array_impl_t* this = (index_array_impl_t*)iface;
    uint32_t index = GET_INDEX_FROM_KEY(key);
    if(index >= this->length)
        return NULL;
    index_array_slot_t* slot = GET_SLOT_FROM_ARRAY(this, index);
    if(slot->sequence_number != GET_SEQUENCE_FROM_KEY(key))
        return NULL;
    return slot->data;
}

static uint32_t get_next_sequence_number(index_array_impl_t* this)
{
    this->sequence_number++;
    if(this->sequence_number == 0)
        this->sequence_number++;
    return this->sequence_number;
}
