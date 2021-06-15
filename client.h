//
// Created by taisia on 25.05.2021.
//

#ifndef SPO_LAB3_CLIENT_H
#define SPO_LAB3_CLIENT_H
#include "util.h"

int start_client_session(int argc, char **argv);

int get_client_socket(char const *address, unsigned short port);
int send_to_client(int sock, client_text_message_t *msg);
int receive_server_message(int sock, server_message_t *out);

#endif //SPO_LAB3_CLIENT_H
