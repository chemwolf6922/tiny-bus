#pragma once

/**
 * This is intended for client use
 */

#include <stdint.h>
#include <tev/tev.h>

typedef void (*tbus_subscribe_callback_t)(const char* topic, const uint8_t* data, uint32_t len, void* ctx);
typedef struct tbus_s tbus_t;

struct tbus_s
{
    void (*close)(tbus_t* self);
    int (*subscribe)(tbus_t* self, const char* topic, tbus_subscribe_callback_t callback, void* ctx);
    void (*unsubscribe)(tbus_t* self, const char* topic);
    int (*publish)(tbus_t* self, const char* topic, const uint8_t* data, uint32_t len);
    struct
    {
        /** 
         * This will not be called if the connection is closed by calling close.
         * The client should not be used after this callback is called.
         */
        void (*on_disconnect)(void* ctx);
        void* on_disconnect_ctx;
    } callbacks;
};

tbus_t* tbus_connect(tev_handle_t tev, const char* uds_path);
