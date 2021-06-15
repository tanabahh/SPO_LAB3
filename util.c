#include "util.h"
#include <ncurses.h>
#include <stdlib.h>
#include "util.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

list_t *list_new(void *data) {
    list_t *node = malloc(sizeof(list_t));
    if (!node) {
        perror(__FILE__ " out of memory");
        return NULL;
    }
    node->next = NULL;
    node->prev = NULL;
    node->data = data;
    return node;
}

list_t *list_insert_after(list_t *node, void *data) {
    list_t *new_ = list_new(data);
    if (!new_)
        return NULL;
    new_->prev = node;
    if (node) {
        new_->next = node->next;
        node->next = new_;
        if (new_->next)
            new_->next->prev = new_;
    }
    return new_;
}

list_t *list_insert_before(list_t *node, void *data) {
    list_t *new_ = list_new(data);
    if (!new_)
        return NULL;
    new_->next = node;
    if (node) {
        new_->prev = node->prev;
        node->prev = new_;
        if (new_->prev)
            new_->prev->next = new_;
    }
    return new_;
}

void list_remove(list_t *node) {
    if (node->next)
        node->next->prev = node->prev;
    if (node->prev)
        node->prev->next = node->next;
    free(node);
}

void render_message(char *buf, size_t buflen, server_message_t *msg) {
    char *ptr = buf;
    size_t shift;
    struct tm *timestamp = localtime(&msg->timestamp);
    shift = snprintf(ptr, buflen, "[%02d:%02d:%02d] ", timestamp->tm_hour,
                     timestamp->tm_min, timestamp->tm_sec);
    ptr += shift;
    buflen -= shift;

    switch (msg->type) {
        case SMT_MESSAGE: {
            shift = snprintf(ptr, buflen, "[%s]", msg->text.sender_name);
            ptr += shift;
            buflen -= shift;
            if (msg->text.receiver_name_len) {
                shift = snprintf(ptr, buflen, " -> [%s]", msg->text.receiver_name);
                ptr += shift;
                buflen -= shift;
            }
            shift = snprintf(ptr, buflen, ": %s\n", msg->text.text);
        } break;
        case SMT_JOIN_NOTIFIC:
            shift = snprintf(ptr, buflen, "Member joined: [%s]\n", msg->member.name);
            break;
        case SMT_LEAVE_NOTIFIC:
            shift = snprintf(ptr, buflen, "Member left: [%s]\n", msg->member.name);
            break;
    }
}

void display_message(WINDOW *window, server_message_t *msg) {
    static char buf[80 * 25 + 1];
    render_message(buf, sizeof(buf), msg);
    waddnstr(window, buf, sizeof(buf));
}

int free_connection(int sock) {
    int shutdown_retcode = shutdown(sock, SHUT_RDWR);
    close(sock);
    return 0;
}

int send_string(int sock, uint32_t len, char *str, int flags) {
    uint32_t len_to_send = htonl(len);
    send(sock, &len_to_send, sizeof(uint32_t), flags | MSG_MORE);
    send(sock, str, len, flags);
    return 1;
}

int receive_string(int sock, uint32_t *len_out, char **str_out) {
    recv(sock, len_out, sizeof(uint32_t), MSG_WAITALL);
    nryl(*len_out);
    *str_out = malloc(*len_out + 1);
    if (!*str_out) {
        perror(__FILE__ " out of memory");
        return -1;
    }
    if (*len_out)
        recv(sock, *str_out, *len_out, MSG_WAITALL);
    str_out[0][*len_out] = 0;
    return 1;
}


