#include "client.h"
#include "util.h"
#include <memory.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "util.h"
#include <arpa/inet.h>
#include <endian.h>
#include <inttypes.h>
#include <memory.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

int get_client_socket(char const *address, unsigned short port) {
    int sock;
    struct sockaddr_in addr = {0};
    addr.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror(__FILE__ " connect()");
        close(sock);
        return -1;
    }
    return sock;
}

int send_to_client(int sock, client_text_message_t *msg) {
    send_string(sock, msg->receiver_name_len, msg->receiver_name, MSG_MORE);
    send_string(sock, msg->text_len, msg->text, 0);
    return 1;
}

static int receive_to_server(int sock, server_text_message_t *out) {
    receive_string(sock, &out->sender_name_len, &out->sender_name);
    receive_string(sock, &out->receiver_name_len, &out->receiver_name);
    receive_string(sock, &out->text_len, &out->text);
    return 1;
}

static int receive_server_notific(int sock, server_member_notification_t *out) {
    receive_string(sock, &out->length, &out->name);
    return 1;
}

int receive_server_message(int sock, server_message_t *out) {
    int64_t timestamp;
    recv(sock, &timestamp, sizeof(int64_t), MSG_WAITALL));
    recv(sock, &out->id, sizeof(uint32_t), MSG_WAITALL);
    nryl(out->id);
    out->timestamp = be64toh(timestamp);

    uint8_t type;
    recv(sock, &type, sizeof(uint8_t), MSG_WAITALL);
    out->type = type;

    switch (type) {
        case SMT_MESSAGE:
            receive_to_server(sock, &out->text);
            break;
        case SMT_JOIN_NOTIFIC:
        case SMT_LEAVE_NOTIFIC:
            receive_server_notific(sock, &out->member);
            break;
        default:
            fprintf(stderr, "Unknown type: %" PRIu8 "\n", type);
            return -1;
    }
    return 1;
}


typedef struct client_data {
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    pthread_mutex_t cursor_mutex;
    pthread_t thread;
    pthread_t receiver_thread;
    pthread_t sender_thread;
    list_t *history_head;
    list_t *history_tail;
    int sock;
    int cursor;
    int down_pressed;
    atomic_int is_running;
} client_shared_data_t;

#define MSG_BUFLEN (80 * 25 + 1)

typedef struct rendered_message {
    server_message_t msg;
    int height;
    char buffer[MSG_BUFLEN];
} rendered_message_t;

do_routine(receiver_routine, client_shared_data_t *shared) {
    while (atomic_load(&shared->is_running)) {
        rendered_message_t *msg = malloc(sizeof(rendered_message_t));
        msg->height = -1;
        receive_server_message(shared->sock, &msg->msg);
        pthread_mutex_lock(&shared->mutex);

        #define ID(iter) (((server_message_t *)((iter)->data))->id) \
        if (!shared->history_head) {                                \
        shared->history_head = shared->history_tail = list_new(msg);\
        } else if (ID(shared->history_head) > msg->msg.id) {        \
        shared->history_head = list_insert_before(shared->history_head, msg); \
        } else if (ID(shared->history_tail) < msg->msg.id) {        \
        shared->history_tail = list_insert_after(shared->history_tail, msg);  \
        } else {                                                    \
        list_t *iter = shared->history_head;                        \
        while (ID(iter) < msg->msg.id)                              \
        iter = iter->next;                                          \
        list_insert_before(iter, msg);                              \
        }                                                           \
        #undef ID

        pthread_mutex_unlock(&shared->mutex);
        pthread_cond_signal(&shared->cond);
    }
    return 0;
}

static int parse_line(client_shared_data_t *shared, char *buf, size_t len) {
    if (buf[0] == '/') {
        if (buf[1] == 'q') {
            atomic_store(&shared->is_running, 0);
            return 0;
        } else {
            char *arg1 = malloc(len);
            char *arg2 = malloc(len);
            if (sscanf(buf, "/p %s %[^\n]", arg1, arg2)) {
                client_text_message_t msg;
                msg.receiver_name_len = strlen(arg1);
                msg.receiver_name = arg1;
                msg.text_len = strlen(arg2);
                msg.text = arg2;
                send_to_client(shared->sock, &msg);
            }
            free(arg1);
            free(arg2);
        }
    } else {
        client_text_message_t msg;
        msg.receiver_name_len = 0;
        msg.text_len = len;
        msg.text = buf;
        send_to_client(shared->sock, &msg);
    }
    return 1;
}

do_routine(sender_routine, client_shared_data_t *shared) {
    int i;
    int height, width;
    getmaxyx(stdscr, height, width);
    WINDOW *window = newwin(1, width, height - 1, 0);
    keypad(window, TRUE);
    char *line = malloc(width - 1);
    size_t pos = 0;
    while (atomic_load(&shared->is_running)) {
        int c = wgetch(window);
        pthread_mutex_lock(&shared->cursor_mutex);
        switch (c) {
            case KEY_BACKSPACE:
                case KEY_DC:
                    case 127:
                        if (pos) {
                            line[--pos] = 0;
                            int y, x;
                            getyx(window, y, x);
                            mvwdelch(window, y, x - 1);
                        }
                        break;
                        case '\n':
                            if (pos)
                                parse_line(shared, line, pos);
                            line[pos = 0] = 0;
                            wclear(window);
                            wrefresh(window);
                            break;
                            case KEY_UP:
                                shared->down_pressed--;
                                pthread_cond_signal(&shared->cond);
                                break;
                                case KEY_DOWN:
                                    shared->down_pressed++;
                                    pthread_cond_signal(&shared->cond);
                                    break;
                                    case KEY_PPAGE:
                                        shared->down_pressed -= getmaxy(stdscr) - 2;
                                        pthread_cond_signal(&shared->cond);
                                        break;
                                        case KEY_NPAGE:
                                            shared->down_pressed += getmaxy(stdscr) - 2;
                                            pthread_cond_signal(&shared->cond);
                                            break;
                                            case KEY_END:
                                                shared->cursor = -1;
                                                shared->down_pressed = 0;
                                                pthread_cond_signal(&shared->cond);
                                                break;
                                                case KEY_HOME:
                                                    shared->cursor = 0;
                                                    shared->down_pressed = 0;
                                                    pthread_cond_signal(&shared->cond);
                                                    break;

                                                    default:
                                                        if (0x20 <= c && c < 0x7f) {
                                                            if (pos < width - 1) {
                                                                waddch(window, c);
                                                                line[pos++] = c;
                                                                line[pos] = 0;
                                                            }
                                                        }
        }
        pthread_mutex_unlock(&shared->cursor_mutex);
    }
    free(line);
    free_connection(shared->sock);
    return 0;
}

static void prepare_message(rendered_message_t *msg, int window_width) {
    render_message(msg->buffer, MSG_BUFLEN, &msg->msg);
    int x = 0;
    msg->height = 0;
    for (char *c = msg->buffer; *c; ++c) {
        if (0x20 <= *c && *c <= 0x7f) {
            if (++x == window_width)
                msg->height++;
        } else if ('\n' == *c) {
            x = 0;
            msg->height++;
        }
    }
}

static int display_part(WINDOW *window, rendered_message_t *msg) {
    int maxy = getmaxy(window);
    char *c;
    for (c = msg->buffer; *c; ++c) {
        if (getcury(window) < maxy - 1) {
            // wprintw(window, "%d %d %c\n", getcury(window), maxy, *c);
            waddch(window, *c);
        }
    }
    return getcury(window) < maxy - 1;
}

static int min(int a, int b) { return a < b ? a : b; }
static int max(int a, int b) { return a > b ? a : b; }

static int find_max_cursor_pos(list_t *head, int height) {
    int max_pos = 0;
    list_t *iter;
    for (iter = head; iter; iter = iter->next)
        max_pos += ((rendered_message_t *)iter->data)->height;
    return max(0, max_pos - height);
}

static int find_real_cursor_pos(int cursor, int max_pos) {
    return max(0, cursor < 0 ? max_pos : min(cursor, max_pos));
}

do_routine(display_routine, client_shared_data_t *shared) {
    int height, width;
    getmaxyx(stdscr, height, width);
    int winh = height - 2;
    int winw = width;
    WINDOW *window = newwin(winh + 1, winw, 0, 0);
    scrollok(window, TRUE);
    pthread_mutex_lock(&shared->mutex);
    while (atomic_load(&shared->is_running)) {
        pthread_cond_wait(&shared->cond, &shared->mutex);
        list_t *iter;
        for (iter = shared->history_head; iter; iter = iter->next) {
            rendered_message_t *msg = iter->data;
            if (msg->height == -1)
                prepare_message(msg, winw);
        }
        pthread_mutex_lock(&shared->cursor_mutex);

        int cursor = shared->cursor;
        int max_pos = find_max_cursor_pos(shared->history_head, winh);
        cursor = find_real_cursor_pos(cursor, max_pos);
        cursor = max(0, cursor + shared->down_pressed);
        cursor = find_real_cursor_pos(cursor, max_pos);
        if (shared->down_pressed) {
            shared->cursor = cursor;
            shared->down_pressed = 0;
        }
        pthread_mutex_unlock(&shared->cursor_mutex);

        iter = shared->history_head;
        int shift = 0;
        while (iter && shift < cursor) {
            shift += ((rendered_message_t *)iter->data)->height;
            iter = iter->next;
        }
        wclear(window);
        wmove(window, 0, 0);
        if (iter) {
            display_part(window, iter->data);
            wscrl(window, shift - cursor);
            iter = iter->next;
        }

        while (iter && display_part(window, iter->data))
            iter = iter->next;
        if (max_pos)
            wprintw(window, "%d lines above, %d below", shift, max_pos - shift);
        wrefresh(window);
    }
    pthread_mutex_unlock(&shared->mutex);

    delwin(window);
    return 0;
}

int start_client_session(int argc, char **argv) {
    if (argc < 5) return -1;

    client_shared_data_t shared;
    shared.sock = get_client_socket(argv[3], atoi(argv[4]));
    send_string(shared.sock, strlen(argv[2]), argv[2], 0);
    pthread_cond_init(&shared.cond, NULL);
    pthread_mutex_init(&shared.mutex, NULL);
    pthread_mutex_init(&shared.cursor_mutex, NULL);
    shared.history_head = NULL;
    shared.history_tail = NULL;
    shared.cursor = -1;
    shared.down_pressed = 0;
    atomic_store(&shared.is_running, 1);
    pthread_create(&shared.receiver_thread, NULL,
                                  receiver_routine_voidptr, &shared);
    pthread_create(&shared.sender_thread, NULL,
                                  sender_routine_voidptr, &shared);
    pthread_create(&shared.thread, NULL,
                                  display_routine_voidptr, &shared);
    pthread_join(shared.receiver_thread, NULL);
    pthread_join(shared.sender_thread, NULL);
    pthread_cond_signal(&shared.cond);
    pthread_join(shared.thread, NULL);
    pthread_mutex_destroy(&shared.mutex);
    pthread_mutex_destroy(&shared.cursor_mutex);
    return 0;
}

