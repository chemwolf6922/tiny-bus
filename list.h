#pragma once

typedef struct list_head_s list_head_t;

struct list_head_s
{
    list_head_t* next;
    list_head_t* prev;
};

#define LIST_HEAD_INIT(node) { &(node), &(node) }

#define LIST_EMPTY(list) ((list)->next == (list))

#define LIST_LINK(list, node) \
    do \
    {\
        (node)->next = (list)->next;\
        (node)->prev = (list);\
        (list)->next->prev = (node);\
        (list)->next = (node);\
    } while(0)

#define LIST_UNLINK(node) \
    do \
    {\
        (node)->next->prev = (node)->prev;\
        (node)->prev->next = (node)->next;\
    } while(0)

#define LIST_FOR_EACH(list, node) \
    for (list_head_t* node = (list)->next; (node) != (list); node = (node)->next)

#define LIST_FOR_EACH_SAFE(list, node) \
    for (list_head_t* node = (list)->next, *tmp3k35rcg = (node)->next; (node) != (list); node = tmp3k35rcg, tmp3k35rcg = node->next)
