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
set(CMAKE_AUTORCC ON)

option(OMANOTES_ENABLE_SANITIZERS "Enable address and undefined behaviour sanitizers" OFF)
option(OMANOTES_ENABLE_FUZZERS "Build the libFuzzer harnesses under fuzz/ (Clang only)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Test)
find_package(KF6TextEditor 6.9 REQUIRED)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
if(OMANOTES_ENABLE_FUZZERS)
    add_subdirectory(fuzz)
endif()

# What an install ships, and nothing else: the binary, the desktop entry
# whose name matches the Wayland app_id set in main.cpp, the icon, and the
# licence. Tests, fuzzers, and build output are never installed (ADR 0013).
# The licence directory is a cache variable so an Arch package can point it
# at /usr/share/licenses/<pkgname>, which is where pacman policy wants it.
include(GNUInstallDirs)
set(OMANOTES_LICENSE_DIR
    "${CMAKE_INSTALL_DATADIR}/licenses/omanotes"
    CACHE STRING "Where LICENSE is installed, relative to the install prefix"
)
install(TARGETS omanotes RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES packaging/linux/OmaNotes.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES packaging/linux/OmaNotes.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
install(FILES LICENSE DESTINATION "${OMANOTES_LICENSE_DIR}")

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)
find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy REQUIRED)

# Every C++ file in the tree is format-checked; the list is discovered, not
# kept by hand, so a new file cannot be missed (threat-model finding F-19).
file(
    GLOB_RECURSE OMANOTES_FORMAT_FILES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/*.cpp"
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
