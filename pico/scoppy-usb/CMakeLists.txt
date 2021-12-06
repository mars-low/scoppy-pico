if (TARGET tinyusb_device_unmarked)
    add_library(scoppy_usb INTERFACE)

    target_include_directories(scoppy_usb INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

    target_sources(scoppy_usb INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/scoppy_usb.c
        ${CMAKE_CURRENT_LIST_DIR}/scoppy_usb_descriptors.c
    )

    target_link_libraries(scoppy_usb INTERFACE
        tinyusb_device_unmarked
        pico_stdio
        pico_time
    )

    target_compile_definitions(scoppy_usb INTERFACE
            SCOPPY_USB=1
    )
endif()
