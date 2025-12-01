cmake_minimum_required(VERSION 3.18)

# Find Qt6 Test components
find_package(Qt6 REQUIRED COMPONENTS Test)

# Basic sanity tests
add_executable(pcmanfm-qt-basic-tests
    test_basic.cpp
)

target_link_libraries(pcmanfm-qt-basic-tests
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_include_directories(pcmanfm-qt-basic-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/pcmanfm
)

add_test(NAME pcmanfm-qt-basic-tests COMMAND pcmanfm-qt-basic-tests)
set_tests_properties(pcmanfm-qt-basic-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# POSIX filesystem core tests
add_executable(pcmanfm-qt-fs-tests
    fs_ops_test.cpp
    ../src/core/fs_ops.cpp
)

target_link_libraries(pcmanfm-qt-fs-tests
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${BLAKE3_LIBRARIES}
)

target_include_directories(pcmanfm-qt-fs-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/pcmanfm
    ${BLAKE3_INCLUDE_DIRS}
)

add_test(NAME pcmanfm-qt-fs-tests COMMAND pcmanfm-qt-fs-tests)
set_tests_properties(pcmanfm-qt-fs-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Qt adapter (QtFileOps) tests
add_executable(pcmanfm-qt-ops-tests
    qt_fileops_test.cpp
    ../src/backends/qt/qt_fileops.cpp
    ../src/core/ifileops.cpp
    ../src/core/fs_ops.cpp
)

target_link_libraries(pcmanfm-qt-ops-tests
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${BLAKE3_LIBRARIES}
)

target_include_directories(pcmanfm-qt-ops-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/pcmanfm
    ${BLAKE3_INCLUDE_DIRS}
)

add_test(NAME pcmanfm-qt-ops-tests COMMAND pcmanfm-qt-ops-tests)
set_tests_properties(pcmanfm-qt-ops-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Archive extraction tests
add_executable(pcmanfm-qt-archive-tests
    archive_extract_test.cpp
    ../src/core/archive_extract.cpp
    ../src/core/fs_ops.cpp
)

target_link_libraries(pcmanfm-qt-archive-tests
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${LIBARCHIVE_LIBRARIES}
    ${BLAKE3_LIBRARIES}
)

target_include_directories(pcmanfm-qt-archive-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/pcmanfm
    ${LIBARCHIVE_INCLUDE_DIRS}
    ${BLAKE3_INCLUDE_DIRS}
)

add_test(NAME pcmanfm-qt-archive-tests COMMAND pcmanfm-qt-archive-tests)
set_tests_properties(pcmanfm-qt-archive-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Windowed file reader tests
add_executable(pcmanfm-qt-windowed-reader-tests
    windowed_file_reader_test.cpp
    ../src/core/windowed_file_reader.cpp
)

target_link_libraries(pcmanfm-qt-windowed-reader-tests
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_include_directories(pcmanfm-qt-windowed-reader-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME pcmanfm-qt-windowed-reader-tests COMMAND pcmanfm-qt-windowed-reader-tests)
set_tests_properties(pcmanfm-qt-windowed-reader-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Disassembly engine tests
add_executable(pcmanfm-qt-disasm-tests
    disasm_engine_test.cpp
    ../src/ui/disasm_engine.cpp
)

target_link_libraries(pcmanfm-qt-disasm-tests
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${CAPSTONE_LIBRARIES}
)

target_include_directories(pcmanfm-qt-disasm-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CAPSTONE_INCLUDE_DIRS}
)

add_test(NAME pcmanfm-qt-disasm-tests COMMAND pcmanfm-qt-disasm-tests)
set_tests_properties(pcmanfm-qt-disasm-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
