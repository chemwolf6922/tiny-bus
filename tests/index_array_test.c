#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../index_array.h"

typedef struct {
    int value;
} test_data_t;

void clear_data(void* data, void* ctx) {
    (void)ctx; // Unused
    ((test_data_t*)data)->value = 0;
}

void test_index_array() {
    index_array_t* array = INDEX_ARRAY_NEW(test_data_t);
    assert(array != NULL);

    // Test allocation and retrieval
    index_array_key_t key1, key2;
    test_data_t* data1 = (test_data_t*)array->alloc(array, &key1);
    test_data_t* data2 = (test_data_t*)array->alloc(array, &key2);
    assert(data1 != NULL);
    assert(data2 != NULL);

    data1->value = 42;
    data2->value = 84;

    test_data_t* fetched1 = (test_data_t*)array->get(array, key1);
    test_data_t* fetched2 = (test_data_t*)array->get(array, key2);
    assert(fetched1 != NULL);
    assert(fetched2 != NULL);
    assert(fetched1->value == 42);
    assert(fetched2->value == 84);

    // Test deallocation and boundary conditions
    test_data_t old_data;
    array->free(array, key1, &old_data);
    assert(old_data.value == 42);
    assert(array->get(array, key1) == NULL);

    // Test reallocation
    test_data_t* data3 = (test_data_t*)array->alloc(array, &key1);
    assert(data3 != NULL);
    data3->value = 126;
    test_data_t* fetched3 = (test_data_t*)array->get(array, key1);
    assert(fetched3 != NULL);
    assert(fetched3->value == 126);

    // Check segment growth and cleanup
    test_data_t* data_array[50];
    index_array_key_t keys[50];
    for (int i = 0; i < 50; i++) {
        data_array[i] = (test_data_t*)array->alloc(array, &keys[i]);
        assert(data_array[i] != NULL);
        data_array[i]->value = i;
    }

    for (int i = 0; i < 50; i++) {
        test_data_t* fetched = (test_data_t*)array->get(array, keys[i]);
        assert(fetched != NULL);
        assert(fetched->value == i);
    }

    for (int i = 0; i < 50; i++) {
        array->free(array, keys[i], NULL);
    }

    // Cleanup
    array->destroy(array, clear_data, NULL);
}

int main(void) {
    test_index_array();
    printf("All tests passed!\n");
    return 0;
}
