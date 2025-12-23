#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <debug/log.h>

typedef struct list_entry {
    struct list_entry* next;
    struct list_entry* prev;
} list_entry_t;

typedef list_entry_t list_t;

#define LIST_INIT(head) \
    (list_entry_t){ .next = head, .prev = head }

#define containerof(ptr, type, member) \
    ((type*)((uint8_t*) (ptr) - offsetof(type, member)))

static inline void list_init(list_t* head) {
    head->next = head;
    head->prev = head;
}

static inline void __list_add(list_entry_t* new, list_entry_t* prev, list_entry_t* next) {
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

static inline void list_add(list_t* head, list_entry_t* new) {
    __list_add(new, head, head->next);
}

static inline void list_add_tail(list_t* head, list_entry_t* new) {
    __list_add(new, head->prev, head);
}

static inline void __list_del(list_entry_t* prev, list_entry_t* next) {
    next->prev = prev;
    prev->next = next;
}

static inline void list_del(list_entry_t* entry) {
    __list_del(entry->prev, entry->next);
    entry->next = (void*)0xdead000000000000;
    entry->prev = (void*)0xdead000010000000;
}

static inline bool list_is_empty(list_t* head) {
    return head->next == head;
}

static inline bool list_is_head(const list_t* head, const list_entry_t* list) {
    return list == head;
}

static inline list_entry_t* list_pop(list_t* head) {
    if (list_is_empty(head)) {
        return NULL;
    }

    list_entry_t* entry = head->next;
    list_del(entry);
    return entry;
}

#define list_entry_is_head(pos, head, member)				\
	list_is_head((head), &pos->member)

#define list_entry(ptr, type, member) \
	containerof(ptr, type, member)

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_for_each_entry(pos, head, member) \
	for (pos = list_first_entry(head, typeof(*pos), member); \
	     !list_entry_is_head(pos, head, member); \
	     pos = list_next_entry(pos, member))

/**
 * list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_first_entry(head, typeof(*pos), member),	\
		n = list_next_entry(pos, member);			\
	     !list_entry_is_head(pos, head, member); 			\
	     pos = n, n = list_next_entry(n, member))
