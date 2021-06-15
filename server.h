//
// Created by taisia on 25.05.2021.
//

#ifndef SPO_LAB3_SERVER_H
#define SPO_LAB3_SERVER_H
#include "util.h"

int start_server_session(int argc, char **argv);

int init_server_socket(unsigned short port);
int accept_connection(int sock);
int send_server_message(int sock, server_message_t *msg, int flags);
int receive_client_text_message(int sock, client_text_message_t *out);

#endif //SPO_LAB3_SERVER_H
