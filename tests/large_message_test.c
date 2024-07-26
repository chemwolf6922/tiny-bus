#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include "../tbus.h"

static tev_handle_t tev = NULL;
static tbus_t* client = NULL;
static uint8_t random_data[10 * 1024 * 1024];

static void on_message(const char* topic, const uint8_t* data, uint32_t len, void* ctx)
{
    assert(len == sizeof(random_data));
    for(int i = 0; i < len ; i++)
    {
        if(data[i] != random_data[i])
        {
            fprintf(stderr, "data[%d] = %d, random_data[%d] = %d\n", i, data[i], i, random_data[i]);
            abort();
        }
    }
    client->close(client);
}

int main(int argc, char const *argv[])
{
    /** prepare test data */
    FILE* urandom = fopen("/dev/urandom", "r");
    assert(urandom);
    size_t bytes_read = fread(random_data, 1, sizeof(random_data), urandom);
    fclose(urandom);
    assert(bytes_read == sizeof(random_data));

    /** run test */
    tev = tev_create_ctx();
    assert(tev);
    client = tbus_connect(tev, NULL);
    assert(client);
    client->subscribe(client, "test", on_message, NULL);
    client->publish(client, "test", random_data, sizeof(random_data));
    tev_main_loop(tev);
    tev_free_ctx(tev);
    return 0;
}

