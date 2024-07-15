#pragma once

#include <stdint.h>

#pragma region ABI

/** 
 * The message version should only increase on ABI breaking changes
 * New TLV types that is optional should not bump the version 
 */
#define TBUS_MSG_VERSION (0)

/** The following section needs to be ABI compatible within the same version */

enum
{
    TBUS_MSG_CMD_SUB,
    TBUS_MSG_CMD_UNSUB,
    TBUS_MSG_CMD_PUB,
    TBUS_MSG_CMD_MAX
};

/** This should never change */
typedef uint32_t tbus_message_len_t;
typedef uint8_t tbus_message_command_t;
typedef struct
{
    /** len is the size of the whole packet, includes sizeof(len) */
    tbus_message_len_t len;
    uint8_t version;
    tbus_message_command_t command;
    uint8_t options[2];
    /** data in TLV format */
    uint8_t data[];
}__attribute__((packed)) tbus_message_raw_header_t;

enum
{
    TBUS_MSG_TYPE_TOPIC,
    TBUS_MSG_TYPE_DATA,
    TBUS_MSG_TYPE_SUB_INDEX,
    TBUS_MSG_TYPE_MAX
};

typedef uint64_t tbus_message_sub_index_t;

typedef struct
{
    uint8_t type;
    uint32_t len;
    uint8_t data[];
}__attribute__((packed)) tbus_message_raw_tlv_t;

#pragma endregion

#pragma region API

typedef struct
{
    tbus_message_command_t command;
    char* topic;
    /** This is NOT len, this is data's length */
    uint32_t data_len;
    uint8_t* data;
    /** Allow this to be modified by the broker */
    tbus_message_sub_index_t* p_sub_index;
} tbus_message_t;

/**
 * Serialize a message to a buffer
 * @param msg The message to serialize
 * @param len The length of the serialized message
 * @return The serialized message or NULL on failure
 * @note The caller is responsible for freeing the returned buffer
 */
uint8_t* tbus_message_serialize(const tbus_message_t* msg, int* len);
/**
 * Create a view of the message.
 * The view is only valid as long as the original message is valid.
 * Be very careful when modifying the view's content. 
 * @param src The message to view
 * @param src_len The length of the message
 * @param msg The message view
 * @return 0 on success, -1 on failure
 */
int tbus_message_view(const uint8_t* src, int src_len, tbus_message_t* msg);

#pragma endregion
