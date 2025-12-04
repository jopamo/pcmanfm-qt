cmake_minimum_required(VERSION 3.18)
project(menu-cache LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.18.0 gio-2.0 gobject-2.0 gthread-2.0)

set(MC_VERSION_MAJOR 1)
set(MC_VERSION_MINOR 1)
set(MC_VERSION_MICRO 1)
set(MENU_CACHE_VERSION "${MC_VERSION_MAJOR}.${MC_VERSION_MINOR}.${MC_VERSION_MICRO}")

set(MENU_CACHE_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${MENU_CACHE_INCLUDE_DIR}/menu-cache")
set(MENU_CACHE_GENERATED_HEADER "${MENU_CACHE_INCLUDE_DIR}/menu-cache/menu-cache.h")
set(MENU_CACHE_INTERNAL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/libmenu-cache/menu-cache.h")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/libmenu-cache/menu-cache.h.in"
    "${MENU_CACHE_GENERATED_HEADER}"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/libmenu-cache/menu-cache.h.in"
    "${MENU_CACHE_INTERNAL_HEADER}"
    @ONLY
)

add_library(menu-cache STATIC
    libmenu-cache/menu-cache.c
    libmenu-cache/version.h
    ${MENU_CACHE_GENERATED_HEADER}
    ${MENU_CACHE_INTERNAL_HEADER}
)
set_target_properties(menu-cache PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(menu-cache
    PUBLIC
        ${MENU_CACHE_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libmenu-cache
        ${CMAKE_CURRENT_BINARY_DIR}/libmenu-cache
        ${CMAKE_BINARY_DIR}
        ${GLIB_INCLUDE_DIRS}
)
target_link_libraries(menu-cache PUBLIC ${GLIB_LIBRARIES})
target_compile_options(menu-cache PRIVATE ${GLIB_CFLAGS_OTHER})
target_compile_definitions(menu-cache
    PRIVATE
        MENUCACHE_LIBEXECDIR=\"${CMAKE_INSTALL_FULL_LIBEXECDIR}\"
        G_LOG_DOMAIN=\"Menu-Cache\"
)

add_executable(menu-cached
    menu-cache-daemon/menu-cached.c
)
target_include_directories(menu-cached PRIVATE
    ${MENU_CACHE_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libmenu-cache
    ${CMAKE_CURRENT_BINARY_DIR}/libmenu-cache
    ${CMAKE_BINARY_DIR}
    ${GLIB_INCLUDE_DIRS}
)
target_link_libraries(menu-cached PRIVATE menu-cache ${GLIB_LIBRARIES})
target_compile_options(menu-cached PRIVATE ${GLIB_CFLAGS_OTHER})
target_compile_definitions(menu-cached PRIVATE MENUCACHE_LIBEXECDIR=\"${CMAKE_INSTALL_FULL_LIBEXECDIR}\")

add_executable(menu-cache-gen
    menu-cache-gen/main.c
    menu-cache-gen/menu-compose.c
    menu-cache-gen/menu-merge.c
)
target_include_directories(menu-cache-gen PRIVATE
    ${MENU_CACHE_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libmenu-cache
    ${CMAKE_CURRENT_BINARY_DIR}/libmenu-cache
    ${CMAKE_CURRENT_SOURCE_DIR}/menu-cache-gen
    ${CMAKE_SOURCE_DIR}/libfm/src
    ${CMAKE_BINARY_DIR}
    ${GLIB_INCLUDE_DIRS}
)
target_link_libraries(menu-cache-gen PRIVATE menu-cache fm-extra ${GLIB_LIBRARIES})
target_compile_options(menu-cache-gen PRIVATE ${GLIB_CFLAGS_OTHER})
target_compile_definitions(menu-cache-gen PRIVATE MENUCACHE_LIBEXECDIR=\"${CMAKE_INSTALL_FULL_LIBEXECDIR}\")
