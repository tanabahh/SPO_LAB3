#include "client.h"
#include "server.h"
#include <ncurses.h>

int main(int argc, char **argv) {
    printf("A");
    initscr();
    cbreak();
    noecho();
    int curs_state = curs_set(0);
    int retcode = (argv[1][0] == 'c' ? start_client_session : start_server_session)(argc, argv);
    curs_set(curs_state);
    endwin();
    return retcode;
}
