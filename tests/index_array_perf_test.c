#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <tev/map.h>
#include "../index_array.h"

#define NUM_ELEMENTS 10000 // 1 million elements

typedef struct {
    int value;
} test_data_t;

void clear_data(void* data, void* ctx) {
    (void)ctx; // Unused
    ((test_data_t*)data)->value = 0;
}

double get_duration(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

void perf_test_index_array() {
    index_array_t* array = INDEX_ARRAY_NEW(test_data_t);
    if (array == NULL) {
        fprintf(stderr, "Failed to create index array\n");
        exit(EXIT_FAILURE);
    }

    index_array_key_t* keys = (index_array_key_t*)malloc(NUM_ELEMENTS * sizeof(index_array_key_t));
    if (keys == NULL) {
        fprintf(stderr, "Failed to allocate keys array\n");
        array->destroy(array, clear_data, NULL);
        exit(EXIT_FAILURE);
    }

    struct timespec start, end;

    // Test allocation performance
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < NUM_ELEMENTS; i++) {
        test_data_t* data = (test_data_t*)array->alloc(array, &keys[i]);
        if (data == NULL) {
            fprintf(stderr, "Allocation failed at index %zu\n", i);
            free(keys);
            array->destroy(array, clear_data, NULL);
            exit(EXIT_FAILURE);
        }
        data->value = i;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double alloc_duration = get_duration(start, end);
    printf("Allocation of %d elements took %.6f seconds\n", NUM_ELEMENTS, alloc_duration);

    // Test retrieval performance
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < NUM_ELEMENTS; i++) {
        test_data_t* data = (test_data_t*)array->get(array, keys[i]);
        if (data == NULL || data->value != (int)i) {
            fprintf(stderr, "Retrieval failed at index %zu\n", i);
            free(keys);
            array->destroy(array, clear_data, NULL);
            exit(EXIT_FAILURE);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double duration_get = get_duration(start, end);
    printf("Retrieval of %d elements took %.6f seconds\n", NUM_ELEMENTS, duration_get);

    // Test deallocation performance
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < NUM_ELEMENTS; i++) {
        array->free(array, keys[i], NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double free_duration = get_duration(start, end);
    printf("Deallocation of %d elements took %.6f seconds\n", NUM_ELEMENTS, free_duration);

    // Cleanup
    free(keys);
    array->destroy(array, clear_data, NULL);
}

void perf_test_map() {
    map_handle_t map = map_create();
    if (map == NULL) {
        fprintf(stderr, "Failed to create index array\n");
        exit(EXIT_FAILURE);
    }

    test_data_t* data = (test_data_t*)malloc(NUM_ELEMENTS * sizeof(test_data_t));
    if (data == NULL) {
        fprintf(stderr, "Failed to allocate keys array\n");
        map_delete(map, NULL, NULL);
        exit(EXIT_FAILURE);
    }

    struct timespec start, end;

    // Test allocation performance
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < NUM_ELEMENTS; i++) {
        if(map_add(map, &i, sizeof(i), &data[i]) == NULL)
        {
            fprintf(stderr, "Allocation failed at index %zu\n", i);
            free(data);
            map_delete(map, NULL, NULL);
            exit(EXIT_FAILURE);
        }
        data[i].value = i;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double alloc_duration = get_duration(start, end);
    printf("Allocation of %d elements took %.6f seconds\n", NUM_ELEMENTS, alloc_duration);

    // Test retrieval performance
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < NUM_ELEMENTS; i++) {
        test_data_t* data = map_get(map, &i, sizeof(i));
        if (data == NULL || data->value != (int)i) {
            fprintf(stderr, "Retrieval failed at index %zu\n", i);
            free(data);
            map_delete(map, NULL, NULL);
            exit(EXIT_FAILURE);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double duration_get = get_duration(start, end);
    printf("Retrieval of %d elements took %.6f seconds\n", NUM_ELEMENTS, duration_get);

    // Test deallocation performance
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < NUM_ELEMENTS; i++) {
        map_remove(map, &i, sizeof(i));
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double free_duration = get_duration(start, end);
    printf("Deallocation of %d elements took %.6f seconds\n", NUM_ELEMENTS, free_duration);

    // Cleanup
    free(data);
    map_delete(map, NULL, NULL);
}

int main(void) {
    perf_test_index_array();
    perf_test_map();
    return 0;
}
