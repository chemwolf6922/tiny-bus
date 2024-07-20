#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "../message.h"

int main(int argc, char const *argv[])
{
    tbus_message_sub_index_t sub_index = 1;
    tbus_message_t msg = {
        .command = TBUS_MSG_CMD_PUB,
        .topic = "test",
        .p_sub_index = &sub_index,
        .data = "Hello, World!",
        .data_len = 14
    };
    size_t buffer_len = 0;
    uint8_t* buffer = tbus_message_serialize(&msg, &buffer_len);
    assert(buffer != NULL);
    tbus_message_t msg_view;
    assert(tbus_message_view(buffer, buffer_len, &msg_view) == 0);
    assert(msg_view.command == TBUS_MSG_CMD_PUB);
    assert(strcmp(msg_view.topic, "test") == 0);
    tbus_message_sub_index_t sub_index_read;
    READ_SUB_INDEX(&msg_view, sub_index_read);
    assert(sub_index_read == 1);
    assert(msg_view.data_len == 14);
    assert(strcmp(msg_view.data, "Hello, World!") == 0);
    free(buffer);
    return 0;
}

