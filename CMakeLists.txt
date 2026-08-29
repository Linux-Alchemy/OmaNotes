cmake_minimum_required(VERSION 3.28)

project(
    omanotes
    VERSION 0.1.0
    DESCRIPTION "An Omarchy-first Markdown writing application"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

option(OMANOTES_ENABLE_SANITIZERS "Enable address and undefined behaviour sanitizers" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Test)
find_package(KF6TextEditor 6.9 REQUIRED)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)
find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy REQUIRED)

set(OMANOTES_FORMAT_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/editor_adapter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/ktext_editor_adapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/ktext_editor_adapter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main_window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main_window.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/main_window_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/ktext_editor_adapter_test.cpp"
)

add_custom_target(
    format-check
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${OMANOTES_FORMAT_FILES}
    COMMENT "Checking C++ formatting"
    VERBATIM
)

add_custom_target(
    clang-tidy
    COMMAND
        "${RUN_CLANG_TIDY_EXECUTABLE}"
        -p "${CMAKE_BINARY_DIR}"
        "-source-filter=^${CMAKE_SOURCE_DIR}/(src|tests)/"
        -quiet
    DEPENDS omanotes omanotes_editor_tests omanotes_ui_tests
    COMMENT "Running clang-tidy"
    VERBATIM
)

add_custom_target(
    security-check
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/security-check.sh" "$<TARGET_FILE:omanotes>"
    DEPENDS omanotes
    COMMENT "Checking executable hardening"
    VERBATIM
)
