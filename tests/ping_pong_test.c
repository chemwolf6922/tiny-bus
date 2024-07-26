#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include "../tbus.h"

static tev_handle_t tev = NULL;
static tbus_t* a = NULL;
static tbus_t* b = NULL;
static uint64_t counter = 0;
static const char* data = "hello world";

static void on_message(const char*, const uint8_t*, uint32_t, void* ctx)
{
    counter++;
    tbus_t* client = (tbus_t*)ctx;
    char* topic = client == a ? "a/normal/topic/b" : "a/normal/topic/a";
    client->publish(client, topic, data, strlen(data));
}

static void stop_test(void* ctx)
{
    a->close(a);
    b->close(b);
}

int main(int argc, char const *argv[])
{
    tev = tev_create_ctx();
    assert(tev);

    a = tbus_connect(tev,NULL);
    assert(a);
    a->subscribe(a, "a/normal/topic/a", on_message, a);
    b = tbus_connect(tev,NULL);
    assert(b);
    b->subscribe(b, "a/normal/topic/b", on_message, b);
    a->publish(a, "a/normal/topic/b", data, strlen(data));

    tev_set_timeout(tev, stop_test, NULL, 10000);

    tev_main_loop(tev);

    tev_free_ctx(tev);

    printf("%"PRIu64" messages per second\n", counter/10);

    return 0;
}
