add_executable(
    omanotes_ui_tests
    unit/main_window_test.cpp
    "${CMAKE_SOURCE_DIR}/src/editor/editor_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/ui/main_window.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/main_window.hpp"
)

target_link_libraries(omanotes_ui_tests PRIVATE KF6::TextEditor Qt6::Test Qt6::Widgets)
target_include_directories(omanotes_ui_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_ui_tests)
omanotes_enable_hardening(omanotes_ui_tests)
omanotes_enable_sanitizers(omanotes_ui_tests)

add_test(NAME ui-structure COMMAND omanotes_ui_tests)

add_executable(
    omanotes_editor_tests
    unit/ktext_editor_adapter_test.cpp
    "${CMAKE_SOURCE_DIR}/src/editor/editor_adapter.hpp"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/ktext_editor_adapter.hpp"
)

target_link_libraries(omanotes_editor_tests PRIVATE KF6::TextEditor Qt6::Test Qt6::Widgets)
target_include_directories(omanotes_editor_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_editor_tests)
omanotes_enable_hardening(omanotes_editor_tests)
omanotes_enable_sanitizers(omanotes_editor_tests)

add_test(NAME editor-adapter COMMAND omanotes_editor_tests)

add_test(NAME process-startup COMMAND omanotes --smoke-test)

set(
    OMANOTES_TEST_ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen"
    "QT_QPA_PLATFORMTHEME="
    "QT_STYLE_OVERRIDE=Fusion"
    "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1"
    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
)

set_tests_properties(
    editor-adapter ui-structure process-startup
    PROPERTIES ENVIRONMENT "${OMANOTES_TEST_ENVIRONMENT}"
)
