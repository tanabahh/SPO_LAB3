//
// Created by taisia on 25.05.2021.
//

#ifndef SPO_LAB3_UTIL_H
#define SPO_LAB3_UTIL_H

#include <errno.h>
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#define do_routine(name, arg)                                                  \
  static int name(arg);                                                        \
  static void *name##_voidptr(void *ptr) {                                     \
    name(ptr);                                                                 \
    return NULL;                                                               \
  }                                                                            \
  static int name(arg)

typedef struct list {
    struct list *next;
    struct list *prev;
    void *data;
} list_t;

list_t *list_new(void *data);
list_t *list_insert_after(list_t *node, void *data);
list_t *list_insert_before(list_t *node, void *data);
void list_remove(list_t *node);
void render_message(char *buf, size_t buflen, server_message_t *msg);
void display_message(WINDOW *window, server_message_t *msg);

typedef enum server_message_type {
    SMT_MESSAGE,
    SMT_JOIN_NOTIFIC,
    SMT_LEAVE_NOTIFIC
} server_message_type_t;

typedef struct client_text_message {
    uint32_t receiver_name_len;
    uint32_t text_len;
    char *receiver_name;
    char *text;
} client_text_message_t;

typedef struct server_text_message {
    uint32_t sender_name_len;
    uint32_t receiver_name_len;
    uint32_t text_len;
    char *sender_name;
    char *receiver_name;
    char *text;
} server_text_message_t;

typedef struct server_member_notification {
    uint32_t length;
    char *name;
} server_member_notification_t;

typedef struct server_message {
    time_t timestamp;
    uint32_t id;
    server_message_type_t type;
    union {
        server_member_notification_t member;
        server_text_message_t text;
    };
} server_message_t;


#define nryl(x) ((x) = ntohl(x))


int free_connection(int sock);
int send_string(int sock, uint32_t len, char *str, int flags);
int receive_string(int sock, uint32_t *len_out, char **str_out);

#endif //SPO_LAB3_UTIL_H
