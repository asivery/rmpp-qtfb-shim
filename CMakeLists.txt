cmake_minimum_required(VERSION 3.14)
project(qtfb-shim VERSION 0.1.0 LANGUAGES CXX)

set(QTFB_SHIM_BASE_SOURCES
    src/shim.cpp
    src/input-shim.cpp
    src/fb-shim.cpp
    src/qtfb-client/qtfb-client.cpp
    )

add_library(qtfb-shim SHARED ${QTFB_SHIM_BASE_SOURCES})
set_target_properties(qtfb-shim PROPERTIES PREFIX "")

add_library(qtfb-shim-32bit SHARED ${QTFB_SHIM_BASE_SOURCES})
target_compile_definitions(qtfb-shim-32bit PRIVATE _32BITFIXEDINFO)
set_target_properties(qtfb-shim-32bit PROPERTIES PREFIX "")
