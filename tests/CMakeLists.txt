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
