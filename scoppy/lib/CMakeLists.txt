add_compile_options(-Wall -Werror -Wno-error=unused-variable)

add_library(scoppy-libs INTERFACE)
target_sources(scoppy-libs INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-util/number.c
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-util/number.h
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-chunked-ring-buffer.c
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-chunked-ring-buffer.h
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-common.h
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-context.h
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-incoming.c
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-incoming.h
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-message.c
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-message.h
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-outgoing.c
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-outgoing.h
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-ring-buffer.c
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-ring-buffer.h
    ${CMAKE_CURRENT_LIST_DIR}/scoppy-stdio.h
    ${CMAKE_CURRENT_LIST_DIR}/scoppy.c
    ${CMAKE_CURRENT_LIST_DIR}/scoppy.h
)

target_include_directories(scoppy-libs INTERFACE ${CMAKE_CURRENT_LIST_DIR})
