#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include "../tbus.h"

static tev_handle_t tev = NULL;
typedef struct
{
    tbus_t* client;
    int target;
    int count;
    const char* name;
} test_client_t;

#define INIT_TEST_CLIENT(c, t)\
    do\
    {\
        (c)->client = tbus_connect(tev, NULL);\
        assert((c)->client);\
        (c)->target = t;\
        (c)->count = 0;\
        (c)->name = #c;\
    } while (0)

static uint8_t random_data[10 * 1024 * 1024];

static void on_message(const char* topic, const uint8_t* data, uint32_t len, void* ctx)
{
    test_client_t* client = (test_client_t*)ctx;
    assert(len == sizeof(random_data));
    assert(memcmp(data, random_data, len) == 0);
    printf("%s: received message #%d\n", client->name, client->count);
    client->count++;
    if(client->count == client->target)
    {
        printf("%s: received all messages\n", client->name);
        client->client->close(client->client);
    }
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
    test_client_t a, b, c, d;
    INIT_TEST_CLIENT(&a, 4);
    INIT_TEST_CLIENT(&b, 4);
    /** c: early exit */
    INIT_TEST_CLIENT(&c, 6);
    INIT_TEST_CLIENT(&d, 8);
    a.client->subscribe(a.client, "test", on_message, &a);
    b.client->subscribe(b.client, "test", on_message, &b);
    c.client->subscribe(c.client, "test", on_message, &c);
    c.client->subscribe(c.client, "#", on_message, &c);
    d.client->subscribe(d.client, "test", on_message, &d);
    d.client->subscribe(d.client, "test/#", on_message, &d);
    a.client->publish(a.client, "test", random_data, sizeof(random_data));
    a.client->publish(a.client, "test", random_data, sizeof(random_data));
    a.client->publish(a.client, "test", random_data, sizeof(random_data));
    a.client->publish(a.client, "test", random_data, sizeof(random_data));

    tev_main_loop(tev);
    tev_free_ctx(tev);
    return 0;
}
