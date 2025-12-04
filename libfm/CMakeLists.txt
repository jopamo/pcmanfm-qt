cmake_minimum_required(VERSION 3.18)
project(libfm-internal LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.50.0 gobject-2.0>=2.50.0)

set(FM_EXTRA_SOURCES
    src/extra/fm-xml-file.c
    src/glib-compat.c
)

set(LIBFM_INTREE_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${LIBFM_INTREE_INCLUDE_DIR}/libfm")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fm-extra.h"
    "${LIBFM_INTREE_INCLUDE_DIR}/libfm/fm-extra.h"
    COPYONLY
)
set(FM_VERSION_MAJOR 1)
set(FM_VERSION_MINOR 4)
set(FM_VERSION_MICRO 0)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fm-version.h.in"
    "${LIBFM_INTREE_INCLUDE_DIR}/libfm/fm-version.h"
    @ONLY
)

add_library(fm-extra STATIC ${FM_EXTRA_SOURCES})
set_target_properties(fm-extra PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(fm-extra
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/extra
        ${CMAKE_CURRENT_SOURCE_DIR}/src/base
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gio
        ${LIBFM_INTREE_INCLUDE_DIR}
        ${CMAKE_BINARY_DIR}
        ${GLIB_INCLUDE_DIRS}
)

target_link_libraries(fm-extra PUBLIC ${GLIB_LIBRARIES})
target_compile_options(fm-extra PRIVATE ${GLIB_CFLAGS_OTHER})
target_compile_definitions(fm-extra PRIVATE GETTEXT_PACKAGE=\"\")
