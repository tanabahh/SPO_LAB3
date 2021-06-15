#include "server.h"
#include "server_net.h"
#include "util.h"
#include <memory.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/socket.h>
#include "util.h"
#include <arpa/inet.h>
#include <endian.h>
#include <inttypes.h>
#include <memory.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct data_for_server {
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    pthread_mutex_t history_mutex;
    pthread_mutex_t queue_mutex;
    list_t *connections;
    list_t *queue_head;
    list_t *queue_tail;
    list_t *history;
    pthread_t acceptor_thread;
    pthread_t queue_thread;
    int sock;
    atomic_int is_running;
} server_shared_data_t;

typedef struct server_connection_data {
    pthread_t thread;
    server_shared_data_t *shared;
    char *client_name;
    uint32_t client_name_len;
    atomic_int is_active;
    int conn;
} server_connection_data_t;

do_routine(queue_routine, server_shared_data_t *shared) {
    uint32_t id = 0;
    scrollok(stdscr, TRUE);

    pthread_mutex_lock(&shared->queue_mutex);
    while (atomic_load(&shared->is_running)) {
        pthread_cond_wait(&shared->cond, &shared->queue_mutex);
        while (shared->queue_head) {
            server_message_t *msg = shared->queue_head->data;
            msg->timestamp = time(NULL);
            msg->id = ++id;
            pthread_mutex_lock(&shared->history_mutex);
            shared->history = list_insert_after(shared->history, msg);
            pthread_mutex_unlock(&shared->history_mutex);

            display_message(stdscr, msg);
            refresh();

            list_t *iter;
            for (iter = shared->connections; iter; iter = iter->prev) {
                server_connection_data_t *cd = iter->data;
                if (atomic_load(&cd->is_active)) {
                    if (msg->type != SMT_MESSAGE || !msg->text.receiver_name_len ||
                        !strcmp(msg->text.receiver_name, cd->client_name) ||
                        !strcmp(msg->text.sender_name, cd->client_name))
                        send_server_message(cd->conn, msg, MSG_DONTWAIT);
                }
            }
            shared->queue_head = shared->queue_head->next;
            if (shared->queue_head)
                list_remove(shared->queue_head->prev);
        }
    }
    pthread_mutex_unlock(&shared->queue_mutex);
    return 0;
}

static void queue_append(server_shared_data_t *shared, server_message_t *msg) {
    pthread_mutex_lock(&shared->queue_mutex);
    shared->queue_tail =
            list_insert_after(shared->queue_tail, msg);
    if (!shared->queue_head)
        shared->queue_head = shared->queue_tail;
    pthread_mutex_unlock(&shared->queue_mutex);
    pthread_cond_signal(&shared->cond);
}

do_routine(connection_routine, server_connection_data_t *self) {
    int retcode = 1;
    while (retcode > 0 && atomic_load(&self->is_active) &&
    atomic_load(&self->shared->is_running)) {
        client_text_message_t cmsg;
        retcode = receive_client_text_message(self->conn, &cmsg);
        server_message_t *msg = malloc(sizeof(server_message_t));
        switch (retcode) {
            case 1:
                msg->type = SMT_MESSAGE;
                msg->text.sender_name = self->client_name;
                msg->text.sender_name_len = self->client_name_len;
                msg->text.receiver_name = cmsg.receiver_name;
                msg->text.receiver_name_len = cmsg.receiver_name_len;
                msg->text.text = cmsg.text;
                msg->text.text_len = cmsg.text_len;
                queue_append(self->shared, msg);
                break;
                case 0:
                    msg->type = SMT_LEAVE_NOTIFIC;
                    msg->member.name = self->client_name;
                    msg->member.length = self->client_name_len;
                    queue_append(self->shared, msg);
                    break;
                    case -1:
                        free(msg);
                        break;
        }
    }

    atomic_store(&self->is_active, 0);
    return retcode;
}

do_routine(acceptor_routine, server_shared_data_t *shared) {
    while (atomic_load(&shared->is_running)) {
        int conn;
        conn = accept_connection(shared->sock);
        server_connection_data_t *data = malloc(sizeof(server_connection_data_t));
        int received_name = receive_string(conn, &data->client_name_len, &data->client_name);
        if (received_name == 1) {
            pthread_mutex_lock(&shared->history_mutex);
            data->shared = shared;
            data->conn = conn;
            atomic_store(&data->is_active, 1);
            pthread_create(&data->thread, NULL,
                                          connection_routine_voidptr, data);
            pthread_mutex_lock(&shared->mutex);
            shared->connections = data->list_pos =list_insert_after(shared->connections, data);
            pthread_mutex_unlock(&shared->mutex);

            list_t *iter;
            for (iter = shared->history; iter; iter = iter->prev)
                send_server_message(conn, iter->data, MSG_DONTWAIT);
            pthread_mutex_unlock(&shared->history_mutex);
            server_message_t *msg = malloc(sizeof(server_message_t));
            msg->type = SMT_JOIN_NOTIFIC;
            msg->member.length = data->client_name_len;
            msg->member.name = data->client_name;
            queue_append(shared, msg);
        } else {
            free(data);
        }
    }
    return 0;
}

int start_server_session(int argc, char **argv) {
    scrollok(stdscr, TRUE);
    if (argc < 3)
        return -1;
    server_shared_data_t shared;
    shared.sock = init_server_socket(atoi(argv[2]));

    pthread_cond_init(&shared.cond, NULL);
    pthread_mutex_init(&shared.mutex, NULL);
    pthread_mutex_init(&shared.history_mutex, NULL);
    pthread_mutex_init(&shared.queue_mutex, NULL);

    shared.connections = NULL;
    shared.queue_head = NULL;
    shared.queue_tail = NULL;
    shared.history = NULL;

    atomic_store(&shared.is_running, 1);
    pthread_create(&shared.acceptor_thread, NULL,
                                  acceptor_routine_voidptr, &shared);
    pthread_create(&shared.queue_thread, NULL,
                                  queue_routine_voidptr, &shared);

    int c;
    do {
        c = getch();
    } while (c != 'q' && c != 'Q');

    atomic_store(&shared.is_running, 0);
    pthread_cond_signal(&shared.cond);
    pthread_join(shared.queue_thread, NULL);

    while (shared.connections) {
        list_t *prev = shared.connections->prev;
        server_connection_data_t *data = shared.connections->data;
        atomic_store(&data->is_active, 0);
        free_connection(data->conn);
        pthread_join(data->thread, NULL);
        free(data->client_name);
        free(data);
        list_remove(shared.connections);
        shared.connections = prev;
    }

    free_connection(shared.sock);
    pthread_join(shared.acceptor_thread, NULL);

    pthread_mutex_destroy(&shared.mutex);
    pthread_mutex_destroy(&shared.history_mutex);
    pthread_mutex_destroy(&shared.queue_mutex);

    return 0;
}

int init_server_socket(unsigned short port) {
    int retcode = -1;
    int sock;
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in addr = {0};
    addr.sin_family = PF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror(__FILE__ " bind()");
        close(sock);
        return -1;
    }

    if (listen(sock, SOMAXCONN) == -1) {
        perror(__FILE__ " listen()");
        close(sock);
        return -1;
    }

    return sock;
}

int accept_connection(int sock) {
    int connection;
    connection = accept(sock, NULL, NULL);
    return connection;
}

int send_server_text_message(int sock, server_text_message_t *msg, int flags) {
    send_string(sock, msg->sender_name_len, msg->sender_name,
                       flags | MSG_MORE);
    send_string(sock, msg->receiver_name_len, msg->receiver_name,
                       flags | MSG_MORE);
    send_string(sock, msg->text_len, msg->text, flags);
    return 1;
}

int send_server_message(int sock, server_message_t *msg, int flags) {
    int64_t timestamp = htobe64(msg->timestamp);
    uint32_t id = htonl(msg->id);
    uint8_t type = msg->type;
    send(sock, &timestamp, sizeof(int64_t), MSG_MORE);
    send(sock, &id, sizeof(uint32_t), MSG_MORE);
    send(sock, &type, sizeof(uint8_t), MSG_MORE);
    switch (msg->type) {
        case SMT_MESSAGE:
            send_server_text_message(sock, &msg->text, flags);
            break;
        case SMT_JOIN_NOTIFIC:
        case SMT_LEAVE_NOTIFIC:
            send_string(sock, msg->member.length, msg->member.name, flags);
            break;
    }
    return 1;
}

int receive_client_text_message(int sock, client_text_message_t *out) {
    receive_string(sock, &out->receiver_name_len, &out->receiver_name);
    receive_string(sock, &out->text_len, &out->text);
    return 1;
}


