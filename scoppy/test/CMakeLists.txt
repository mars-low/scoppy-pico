cmake_minimum_required(VERSION 3.12)

#
# cmake -DCMAKE_BUILD_TYPE=Debug ..
#

project(scoppy-libs-test)

# https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
# NB. This applies to this directory and below
add_compile_options(-Wall -Werror -Wno-error=unused-variable -Wno-error=unused-function)

add_subdirectory(../lib scoppy-libs)
add_executable(scoppy-libs-test
    main.c
    simul.c
    simul.h

    fake-serial.c
    fake-serial.h
    scoppy-incoming-test.c
    scoppy-incoming-test.h
    scoppy-message-test.c
    scoppy-message-test.h
    scoppy-outgoing-test.c
    scoppy-outgoing-test.h
    scoppy-chunked-ring-buffer-test.c
    scoppy-ring-buffer-test.c
    scoppy-ring-buffer-test.h
    scoppy-test.h
)

target_link_libraries(scoppy-libs-test PRIVATE scoppy-libs)
