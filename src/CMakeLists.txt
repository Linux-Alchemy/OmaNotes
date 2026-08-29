add_executable(
    omanotes
    main.cpp
    ui/main_window.cpp
    ui/main_window.hpp
)

target_link_libraries(omanotes PRIVATE Qt6::Widgets)
target_include_directories(omanotes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

omanotes_enable_warnings(omanotes)
omanotes_enable_hardening(omanotes)
omanotes_enable_sanitizers(omanotes)
