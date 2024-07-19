#pragma once

typedef struct list_head_s list_head_t;

struct list_head_s
{
    list_head_t* next;
    list_head_t* prev;
};

#define LIST_INIT(node) \
    do \
    {\
        (node)->next = (node);\
        (node)->prev = (node);\
    } while(0)

#define LIST_IS_EMPTY(list) ((list)->next == (list))

/** Link node to the end of list. */
#define LIST_LINK(list, node) \
    do \
    {\
        (node)->prev = (list)->prev;\
        (node)->next = (list);\
        (list)->prev->next = (node);\
        (list)->prev = (node);\
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
