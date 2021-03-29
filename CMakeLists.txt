cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)


project( rmiieth )

pico_sdk_init()

add_executable(rmiieth
        main.c
        rmiieth.c
        rmiieth_md.c
        pkt_queue.c
        pkt_utils.c
)


pico_generate_pio_header(rmiieth ${CMAKE_CURRENT_LIST_DIR}/rmii_ext_clk.pio)

target_include_directories(rmiieth PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(rmiieth PRIVATE pico_stdlib hardware_pio hardware_dma lwip)
pico_add_extra_outputs(rmiieth)
