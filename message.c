#include <stdlib.h>
#include <string.h>
#include "message.h"

/** Avoid unaligned access */
#define WRITE_TLV_SAFE(buffer, offset, type_value, len_value, data_value) \
    do{ \
        tbus_message_raw_tlv_t* tlv_151eqt2 = (tbus_message_raw_tlv_t*)(((uint8_t*)buffer)+offset); \
        tbus_message_raw_tlv_type_t type_value_correct_151eqt2 = (type_value); \
        tbus_message_raw_tlv_len_t len_value_correct_151eqt2 = (len_value); \
        memcpy(&tlv_151eqt2->type, &type_value_correct_151eqt2, sizeof(tlv_151eqt2->type)); \
        memcpy(&tlv_151eqt2->len, &len_value_correct_151eqt2, sizeof(tlv_151eqt2->len)); \
        memcpy(tlv_151eqt2->data, (data_value), (len_value)); \
        offset += sizeof(tbus_message_raw_tlv_t) + tlv_151eqt2->len; \
    }while(0)

uint8_t* tbus_message_serialize(const tbus_message_t* msg, size_t* len)
{
    if(!msg || !len)
        return NULL;
    size_t msg_len = sizeof(tbus_message_raw_header_t);
    /** always pack in a sub index */
    msg_len += sizeof(tbus_message_raw_tlv_t) + sizeof(tbus_message_sub_index_t);
    if(msg->data && msg->data_len > 0)
        msg_len += sizeof(tbus_message_raw_tlv_t) + msg->data_len;
    if(msg->topic)
        msg_len += sizeof(tbus_message_raw_tlv_t) + strlen(msg->topic) + 1 /** \0 */;
    tbus_message_raw_header_t* buffer = (tbus_message_raw_header_t*)malloc(msg_len);
    if(!buffer)
        return NULL;
    memset(buffer, 0, sizeof(tbus_message_raw_header_t));
    buffer->len = msg_len;
    buffer->version = TBUS_MSG_VERSION;
    buffer->command = msg->command;
    size_t offset = 0;
    WRITE_TLV_SAFE(buffer->data, offset, TBUS_MSG_TYPE_SUB_INDEX, sizeof(tbus_message_sub_index_t), msg->p_sub_index);
    if(msg->topic)
    {
        WRITE_TLV_SAFE(buffer->data, offset, TBUS_MSG_TYPE_TOPIC, strlen(msg->topic) + 1, msg->topic);
    }
    if(msg->data && msg->data_len > 0)
    {
        WRITE_TLV_SAFE(buffer->data, offset, TBUS_MSG_TYPE_DATA, msg->data_len, msg->data);
    }
    *len = msg_len;
    return (uint8_t*)buffer;
}


int tbus_message_view(const uint8_t* src, size_t src_len, tbus_message_t* msg)
{
    if(!src || !msg)
        return -1;
    if(src_len < sizeof(tbus_message_raw_header_t))
        return -1;
    tbus_message_raw_header_t* header = (tbus_message_raw_header_t*)src;
    tbus_message_raw_header_t header_view;
    memcpy(&header_view, header, sizeof(header));
    if(header_view.len > src_len)
        return -1;
    if(header_view.version != TBUS_MSG_VERSION)
        return -1;
    msg->command = header_view.command;
    size_t offset = 0;
    size_t data_len = header_view.len - sizeof(tbus_message_raw_header_t);
    while(offset < data_len)
    {
        if(offset + sizeof(tbus_message_raw_tlv_t) > data_len)
            return -1;
        tbus_message_raw_tlv_t* tlv = (tbus_message_raw_tlv_t*)(header->data + offset);
        tbus_message_raw_tlv_t tlv_view;
        memcpy(&tlv_view, tlv, sizeof(tlv_view));
        offset += sizeof(tbus_message_raw_tlv_t);
        if(offset + tlv_view.len > data_len)
            return -1;
        switch(tlv_view.type)
        {
            case TBUS_MSG_TYPE_TOPIC:
                msg->topic = tlv->data;
                break;
            case TBUS_MSG_TYPE_DATA:
                msg->data = tlv->data;
                msg->data_len = tlv_view.len;
                break;
            case TBUS_MSG_TYPE_SUB_INDEX:
                msg->p_sub_index = (tbus_message_sub_index_t*)tlv->data;
                break;
            default:
                return -1;
        }
        offset += tlv->len;
    }
    return 0;
}
