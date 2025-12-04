cmake_minimum_required(VERSION 3.18)

find_package(Qt6 REQUIRED COMPONENTS Test)
include(CMakeParseArguments)

set(PCMANFM_TEST_LIBS
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

set(PCMANFM_TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/pcmanfm
)

function(pcmanfm_add_test target)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;INCLUDES" ${ARGN})

    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${PCMANFM_TEST_LIBS} ${ARG_LIBS})
    target_include_directories(${target} PRIVATE ${PCMANFM_TEST_INCLUDES} ${ARG_INCLUDES})

    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

pcmanfm_add_test(pcmanfm-qt-basic-tests
    SOURCES
        test_basic.cpp
)

pcmanfm_add_test(pcmanfm-qt-core-utils-tests
    SOURCES
        test_core_utils.cpp
)

pcmanfm_add_test(pcmanfm-qt-fs-tests
    SOURCES
        fs_ops_test.cpp
        ../src/core/fs_ops.cpp
    LIBS
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${BLAKE3_INCLUDE_DIRS}
)

pcmanfm_add_test(pcmanfm-qt-ops-tests
    SOURCES
        qt_fileops_test.cpp
        ../src/backends/qt/qt_fileops.cpp
        ../src/core/ifileops.cpp
        ../src/core/fs_ops.cpp
    LIBS
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${BLAKE3_INCLUDE_DIRS}
)

pcmanfm_add_test(pcmanfm-qt-archive-tests
    SOURCES
        archive_extract_test.cpp
        ../src/core/archive_extract.cpp
        ../src/core/fs_ops.cpp
    LIBS
        ${LIBARCHIVE_LIBRARIES}
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${LIBARCHIVE_INCLUDE_DIRS}
        ${BLAKE3_INCLUDE_DIRS}
)

pcmanfm_add_test(pcmanfm-qt-windowed-reader-tests
    SOURCES
        windowed_file_reader_test.cpp
        ../src/core/windowed_file_reader.cpp
)

pcmanfm_add_test(pcmanfm-qt-disasm-tests
    SOURCES
        disasm_engine_test.cpp
        ../src/ui/disasm_engine.cpp
    LIBS
        ${CAPSTONE_LIBRARIES}
    INCLUDES
        ${CAPSTONE_INCLUDE_DIRS}
)

set(PCMANFM_SETTINGS_SOURCES
    ../pcmanfm/settings.cpp
    ../pcmanfm/settings.h
    ../src/ui/fsqt.cpp
    ../src/core/fs_ops.cpp
)

set(PCMANFM_SETTINGS_LIBS
    fm-qt6
    ${BLAKE3_LIBRARIES}
)

set(PCMANFM_SETTINGS_INCLUDES
    ${BLAKE3_INCLUDE_DIRS}
)

pcmanfm_add_test(pcmanfm-qt-utils-tests
    SOURCES
        test_utils.cpp
        ${PCMANFM_SETTINGS_SOURCES}
    LIBS
        ${PCMANFM_SETTINGS_LIBS}
    INCLUDES
        ${PCMANFM_SETTINGS_INCLUDES}
)

pcmanfm_add_test(pcmanfm-qt-settings-tests
    SOURCES
        test_settings.cpp
        ${PCMANFM_SETTINGS_SOURCES}
    LIBS
        ${PCMANFM_SETTINGS_LIBS}
    INCLUDES
        ${PCMANFM_SETTINGS_INCLUDES}
)

pcmanfm_add_test(pcmanfm-qt-settings-functionality-tests
    SOURCES
        test_settings_functionality.cpp
        ${PCMANFM_SETTINGS_SOURCES}
    LIBS
        ${PCMANFM_SETTINGS_LIBS}
    INCLUDES
        ${PCMANFM_SETTINGS_INCLUDES}
)

set(PCMANFM_XDGDIR_SOURCES
    ../pcmanfm/xdgdir.cpp
    ../src/ui/fsqt.cpp
    ../src/core/fs_ops.cpp
)

pcmanfm_add_test(pcmanfm-qt-xdgdir-tests
    SOURCES
        test_xdgdir.cpp
        ${PCMANFM_XDGDIR_SOURCES}
    LIBS
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${BLAKE3_INCLUDE_DIRS}
)

pcmanfm_add_test(pcmanfm-qt-xdgdir-functionality-tests
    SOURCES
        test_xdgdir_functionality.cpp
        ${PCMANFM_XDGDIR_SOURCES}
    LIBS
        ${BLAKE3_LIBRARIES}
    INCLUDES
        ${BLAKE3_INCLUDE_DIRS}
)
