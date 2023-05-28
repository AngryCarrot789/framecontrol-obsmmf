# Frame Control Memory Mapped File
## PLUGIN NAME: fc-mmf

cmake_minimum_required(VERSION 3.16)

project(fc-mmf VERSION 1.0.0)
add_library(fc-mmf MODULE)
set(PLUGIN_AUTHOR "REghZy")

target_sources(
    fc-mmf
    PRIVATE plugin-main.c
            mmf-capture.c
)

target_link_libraries(fc-mmf PRIVATE OBS::libobs OBS::ipc-util OBS::file-updater Jansson::Jansson)

if(MSVC)
  target_link_libraries(fc-mmf PRIVATE OBS::w32-pthreads)
  target_link_options(fc-mmf PRIVATE "LINKER:/IGNORE:4098")
endif()


target_compile_definitions(fc-mmf PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)

set_property(GLOBAL APPEND PROPERTY OBS_MODULE_LIST "fc-mmf")

setup_plugin_target(fc-mmf)
