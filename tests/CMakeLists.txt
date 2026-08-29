add_executable(
    omanotes_ui_tests
    unit/main_window_test.cpp
    "${CMAKE_SOURCE_DIR}/src/ui/main_window.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/main_window.hpp"
)

target_link_libraries(omanotes_ui_tests PRIVATE Qt6::Test Qt6::Widgets)
target_include_directories(omanotes_ui_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

omanotes_enable_warnings(omanotes_ui_tests)
omanotes_enable_hardening(omanotes_ui_tests)
omanotes_enable_sanitizers(omanotes_ui_tests)

add_test(NAME ui-structure COMMAND omanotes_ui_tests)

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
    ui-structure process-startup
    PROPERTIES ENVIRONMENT "${OMANOTES_TEST_ENVIRONMENT}"
)
