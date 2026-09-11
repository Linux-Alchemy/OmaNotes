add_executable(
    omanotes_ui_tests
    unit/main_window_test.cpp
    "${CMAKE_SOURCE_DIR}/src/app/application_controller.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/application_controller.hpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_restorer.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_restorer.hpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.hpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/recovery_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/recovery_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/app/launch_request.hpp"
    "${CMAKE_SOURCE_DIR}/src/app/keymap.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/keymap.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/buffer.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/buffer_registry.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/buffer_registry.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/command.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.hpp"
    "${CMAKE_SOURCE_DIR}/src/app/prefix_router.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/prefix_router.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/editor_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/themes/themes.qrc"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/save_command.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/save_command.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/conflict_detector.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/conflict_detector.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/document_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/document_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/buffer_strip.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/buffer_strip.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/main_window.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/help_overlay.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/help_overlay.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/markdown_view.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/markdown_view.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/view_mode.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_index.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_index.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/text_search.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/text_search.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/search_palette.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/search_palette.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/theme_adapter.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/theme_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/main_window.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/sidebar.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/sidebar.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_tree_model.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_tree_model.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_watcher.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_watcher.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)

target_link_libraries(omanotes_ui_tests PRIVATE KF6::TextEditor Qt6::Test Qt6::Widgets)
target_include_directories(omanotes_ui_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_ui_tests)
omanotes_enable_hardening(omanotes_ui_tests)
omanotes_enable_sanitizers(omanotes_ui_tests)

add_test(NAME ui-structure COMMAND omanotes_ui_tests)

add_executable(
    omanotes_buffer_tests
    unit/buffer_registry_test.cpp
    "${CMAKE_SOURCE_DIR}/src/core/buffer.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/buffer_registry.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/buffer_registry.hpp"
)

target_link_libraries(omanotes_buffer_tests PRIVATE Qt6::Test)
target_include_directories(omanotes_buffer_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_buffer_tests)
omanotes_enable_hardening(omanotes_buffer_tests)
omanotes_enable_sanitizers(omanotes_buffer_tests)

add_test(NAME buffer-registry COMMAND omanotes_buffer_tests)

add_executable(
    omanotes_persistence_tests
    integration/atomic_save_test.cpp
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)

target_link_libraries(omanotes_persistence_tests PRIVATE Qt6::Test)
target_include_directories(omanotes_persistence_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_persistence_tests)
omanotes_enable_hardening(omanotes_persistence_tests)
omanotes_enable_sanitizers(omanotes_persistence_tests)

add_test(NAME atomic-save COMMAND omanotes_persistence_tests)

add_executable(
    omanotes_save_tests
    unit/document_store_test.cpp
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/document_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/document_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)

target_link_libraries(omanotes_save_tests PRIVATE Qt6::Test)
target_include_directories(omanotes_save_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_save_tests)
omanotes_enable_hardening(omanotes_save_tests)
omanotes_enable_sanitizers(omanotes_save_tests)

add_test(NAME document-store COMMAND omanotes_save_tests)

add_executable(
    omanotes_editor_tests
    unit/ktext_editor_adapter_test.cpp
    "${CMAKE_SOURCE_DIR}/src/editor/editor_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/themes/themes.qrc"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.hpp"
)

target_link_libraries(omanotes_editor_tests PRIVATE KF6::TextEditor Qt6::Test Qt6::Widgets)
target_include_directories(omanotes_editor_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_editor_tests)
omanotes_enable_hardening(omanotes_editor_tests)
omanotes_enable_sanitizers(omanotes_editor_tests)

add_test(NAME editor-adapter COMMAND omanotes_editor_tests)

add_executable(
    omanotes_prefix_tests
    unit/prefix_router_test.cpp
    "${CMAKE_SOURCE_DIR}/src/app/prefix_router.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/prefix_router.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.hpp"
)

target_link_libraries(omanotes_prefix_tests PRIVATE Qt6::Test Qt6::Widgets)
target_include_directories(omanotes_prefix_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_prefix_tests)
omanotes_enable_hardening(omanotes_prefix_tests)
omanotes_enable_sanitizers(omanotes_prefix_tests)

add_test(NAME prefix-router COMMAND omanotes_prefix_tests)

add_executable(
    omanotes_command_tests
    unit/command_registry_test.cpp
    "${CMAKE_SOURCE_DIR}/src/core/command.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.hpp"
)

target_link_libraries(omanotes_command_tests PRIVATE Qt6::Test)
target_include_directories(omanotes_command_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_command_tests)
omanotes_enable_hardening(omanotes_command_tests)
omanotes_enable_sanitizers(omanotes_command_tests)

add_test(NAME command-registry COMMAND omanotes_command_tests)

add_executable(
    omanotes_launch_tests
    unit/launch_request_test.cpp
    "${CMAKE_SOURCE_DIR}/src/app/launch_request.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/launch_request.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)

target_link_libraries(omanotes_launch_tests PRIVATE Qt6::Test)
target_include_directories(omanotes_launch_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_launch_tests)
omanotes_enable_hardening(omanotes_launch_tests)
omanotes_enable_sanitizers(omanotes_launch_tests)

add_test(NAME launch-request COMMAND omanotes_launch_tests)

add_executable(
    omanotes_file_tree_tests
    unit/file_tree_model_test.cpp
    "${CMAKE_SOURCE_DIR}/src/workspace/file_tree_model.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_tree_model.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)

target_link_libraries(omanotes_file_tree_tests PRIVATE Qt6::Test)
target_include_directories(omanotes_file_tree_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_file_tree_tests)
omanotes_enable_hardening(omanotes_file_tree_tests)
omanotes_enable_sanitizers(omanotes_file_tree_tests)

add_test(NAME file-tree-model COMMAND omanotes_file_tree_tests)

add_executable(
    omanotes_vim_tests
    integration/vim_behaviour_test.cpp
    "${CMAKE_SOURCE_DIR}/src/editor/editor_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/themes/themes.qrc"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.hpp"
)

target_link_libraries(omanotes_vim_tests PRIVATE KF6::TextEditor Qt6::Test Qt6::Widgets)
target_include_directories(omanotes_vim_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_vim_tests)
omanotes_enable_hardening(omanotes_vim_tests)
omanotes_enable_sanitizers(omanotes_vim_tests)

add_test(NAME vim-behaviour COMMAND omanotes_vim_tests)

add_executable(
    omanotes_conflict_tests
    unit/conflict_detector_test.cpp
    "${CMAKE_SOURCE_DIR}/src/persistence/conflict_detector.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/conflict_detector.hpp"
)

target_link_libraries(omanotes_conflict_tests PRIVATE Qt6::Test)
target_include_directories(omanotes_conflict_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_conflict_tests)
omanotes_enable_hardening(omanotes_conflict_tests)
omanotes_enable_sanitizers(omanotes_conflict_tests)

add_test(NAME conflict-detector COMMAND omanotes_conflict_tests)

add_executable(
    omanotes_watcher_tests
    unit/file_watcher_test.cpp
    "${CMAKE_SOURCE_DIR}/src/workspace/file_watcher.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_watcher.hpp"
)

target_link_libraries(omanotes_watcher_tests PRIVATE Qt6::Test)
target_include_directories(omanotes_watcher_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_watcher_tests)
omanotes_enable_hardening(omanotes_watcher_tests)
omanotes_enable_sanitizers(omanotes_watcher_tests)

add_test(NAME file-watcher COMMAND omanotes_watcher_tests)

add_test(NAME process-startup COMMAND omanotes --smoke-test)

add_executable(omanotes_process_tests integration/launch_process_test.cpp)
target_link_libraries(omanotes_process_tests PRIVATE Qt6::Test)
target_compile_definitions(
    omanotes_process_tests PRIVATE OMANOTES_BINARY_PATH="$<TARGET_FILE:omanotes>"
)
add_dependencies(omanotes_process_tests omanotes)

omanotes_enable_warnings(omanotes_process_tests)
omanotes_enable_hardening(omanotes_process_tests)
omanotes_enable_sanitizers(omanotes_process_tests)

add_test(NAME launch-process COMMAND omanotes_process_tests)

add_executable(omanotes_search_tests
    integration/workspace_search_test.cpp
    "${CMAKE_SOURCE_DIR}/src/workspace/file_index.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/text_search.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/search_palette.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/search_palette.hpp"
)
target_include_directories(omanotes_search_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(omanotes_search_tests PRIVATE Qt6::Test Qt6::Widgets)
omanotes_enable_warnings(omanotes_search_tests)
omanotes_enable_hardening(omanotes_search_tests)
omanotes_enable_sanitizers(omanotes_search_tests)
add_test(NAME workspace-search COMMAND omanotes_search_tests)

add_executable(omanotes_markdown_tests
    integration/markdown_render_test.cpp
    "${CMAKE_SOURCE_DIR}/src/ui/markdown_view.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/markdown_view.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)
target_include_directories(omanotes_markdown_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(
    omanotes_markdown_tests PRIVATE OMANOTES_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
target_link_libraries(omanotes_markdown_tests PRIVATE Qt6::Test Qt6::Widgets)
omanotes_enable_warnings(omanotes_markdown_tests)
omanotes_enable_hardening(omanotes_markdown_tests)
omanotes_enable_sanitizers(omanotes_markdown_tests)
add_test(NAME markdown-render COMMAND omanotes_markdown_tests)

add_executable(omanotes_keymap_tests
    unit/keymap_test.cpp
    "${CMAKE_SOURCE_DIR}/src/app/keymap.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.cpp"
)
target_include_directories(omanotes_keymap_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(omanotes_keymap_tests PRIVATE Qt6::Test Qt6::Widgets)
omanotes_enable_warnings(omanotes_keymap_tests)
omanotes_enable_hardening(omanotes_keymap_tests)
omanotes_enable_sanitizers(omanotes_keymap_tests)
add_test(NAME keymap COMMAND omanotes_keymap_tests)

add_executable(omanotes_theme_tests
    unit/theme_adapter_test.cpp
    "${CMAKE_SOURCE_DIR}/src/ui/theme_adapter.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/theme_adapter.hpp"
)
target_include_directories(omanotes_theme_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(omanotes_theme_tests PRIVATE Qt6::Test Qt6::Widgets)
omanotes_enable_warnings(omanotes_theme_tests)
omanotes_enable_hardening(omanotes_theme_tests)
omanotes_enable_sanitizers(omanotes_theme_tests)
add_test(NAME theme-adapter COMMAND omanotes_theme_tests)

add_executable(omanotes_session_tests
    unit/session_snapshot_test.cpp
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)
target_include_directories(omanotes_session_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(
    omanotes_session_tests PRIVATE OMANOTES_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
target_link_libraries(omanotes_session_tests PRIVATE Qt6::Test Qt6::Widgets)
omanotes_enable_warnings(omanotes_session_tests)
omanotes_enable_hardening(omanotes_session_tests)
omanotes_enable_sanitizers(omanotes_session_tests)
add_test(NAME session-snapshot COMMAND omanotes_session_tests)

add_executable(omanotes_session_store_tests
    integration/session_store_test.cpp
    "${CMAKE_SOURCE_DIR}/src/session/session_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)
target_include_directories(omanotes_session_store_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(omanotes_session_store_tests PRIVATE Qt6::Test Qt6::Widgets)
omanotes_enable_warnings(omanotes_session_store_tests)
omanotes_enable_hardening(omanotes_session_store_tests)
omanotes_enable_sanitizers(omanotes_session_store_tests)
add_test(NAME session-store COMMAND omanotes_session_store_tests)

add_executable(omanotes_recovery_store_tests
    integration/recovery_store_test.cpp
    "${CMAKE_SOURCE_DIR}/src/persistence/recovery_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/recovery_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/conflict_detector.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/conflict_detector.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.hpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)
target_include_directories(omanotes_recovery_store_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(omanotes_recovery_store_tests PRIVATE Qt6::Test Qt6::Widgets)
omanotes_enable_warnings(omanotes_recovery_store_tests)
omanotes_enable_hardening(omanotes_recovery_store_tests)
omanotes_enable_sanitizers(omanotes_recovery_store_tests)
add_test(NAME recovery-store COMMAND omanotes_recovery_store_tests)

add_executable(
    omanotes_session_restore_tests
    integration/session_restore_test.cpp
    "${CMAKE_SOURCE_DIR}/src/app/application_controller.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/application_controller.hpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_restorer.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_restorer.hpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.hpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/recovery_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/recovery_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/app/launch_request.hpp"
    "${CMAKE_SOURCE_DIR}/src/app/keymap.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/keymap.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/buffer.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/buffer_registry.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/buffer_registry.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/command.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.hpp"
    "${CMAKE_SOURCE_DIR}/src/app/prefix_router.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/prefix_router.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/editor_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/themes/themes.qrc"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/save_command.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/save_command.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/conflict_detector.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/conflict_detector.hpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/document_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/document_store.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/buffer_strip.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/buffer_strip.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/main_window.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/help_overlay.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/help_overlay.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/markdown_view.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/markdown_view.hpp"
    "${CMAKE_SOURCE_DIR}/src/core/view_mode.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_index.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_index.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/text_search.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/text_search.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/search_palette.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/search_palette.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/theme_adapter.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/theme_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/main_window.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/sidebar.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/sidebar.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_tree_model.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_tree_model.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_watcher.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/file_watcher.hpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.hpp"
)

target_link_libraries(omanotes_session_restore_tests PRIVATE KF6::TextEditor Qt6::Test Qt6::Widgets)
target_include_directories(omanotes_session_restore_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
omanotes_enable_warnings(omanotes_session_restore_tests)
omanotes_enable_hardening(omanotes_session_restore_tests)
omanotes_enable_sanitizers(omanotes_session_restore_tests)
add_test(NAME session-restore COMMAND omanotes_session_restore_tests)

set(
    OMANOTES_TEST_ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen"
    "QT_QPA_PLATFORMTHEME="
    "QT_STYLE_OVERRIDE=Fusion"
    "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1"
    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
    "XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-cache"
    "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config"
    "XDG_STATE_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-state"
)

set_tests_properties(
    keymap markdown-render workspace-search atomic-save buffer-registry command-registry conflict-detector document-store editor-adapter file-tree-model
    file-watcher launch-process launch-request prefix-router ui-structure process-startup
    vim-behaviour theme-adapter session-snapshot session-store recovery-store session-restore
    PROPERTIES ENVIRONMENT "${OMANOTES_TEST_ENVIRONMENT}"
)
