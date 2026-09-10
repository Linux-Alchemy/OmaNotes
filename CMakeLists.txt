cmake_minimum_required(VERSION 3.28)

project(
    omanotes
    VERSION 0.1.0
    DESCRIPTION "An Omarchy-first Markdown writing application"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/keymap_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/workspace_search_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/application_controller.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/application_controller.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/session/session_restorer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/session/session_restorer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/session_restore_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/session/session_snapshot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/session/session_snapshot.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/session/session_store.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/session/session_store.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/recovery_store.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/recovery_store.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/recovery_store_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/session_store_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/session_snapshot_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/prefix_router.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/prefix_router.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/launch_request.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/keymap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/keymap.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/launch_request.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/buffer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/buffer_registry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/buffer_registry.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/editor_adapter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/ktext_editor_adapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/ktext_editor_adapter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/save_command.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/editor/save_command.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/atomic_file_writer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/atomic_file_writer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/document_store.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/document_store.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/buffer_strip.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/buffer_strip.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main_window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/help_overlay.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/help_overlay.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/file_index.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/file_index.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/text_search.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/text_search.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/search_palette.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/search_palette.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main_window.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/sidebar.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/sidebar.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/file_tree_model.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/file_tree_model.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/workspace_root.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/buffer_registry_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/document_store_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/atomic_save_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/launch_request_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/file_tree_model_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/main_window_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/launch_process_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/ktext_editor_adapter_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/prefix_router_test.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/vim_behaviour_test.cpp"
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
    DEPENDS
        omanotes
        omanotes_keymap_tests
        omanotes_search_tests
        omanotes_buffer_tests
        omanotes_editor_tests
        omanotes_persistence_tests
        omanotes_save_tests
        omanotes_prefix_tests
        omanotes_process_tests
        omanotes_ui_tests
        omanotes_vim_tests
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
