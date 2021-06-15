cmake_minimum_required(VERSION 3.1)
project(spo-lab3 C)
find_package(Threads REQUIRED)
find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIRS})
add_executable(main main.c
        util.h util.c
        client.h client.c
        server.h server.c
        )
target_link_libraries(main Threads::Threads ${CURSES_LIBRARIES})
