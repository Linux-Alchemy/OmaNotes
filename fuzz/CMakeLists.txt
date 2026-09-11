# libFuzzer harnesses for every parser that takes untrusted bytes (block
# 8.1.2). Built only with -DOMANOTES_ENABLE_FUZZERS=ON (the `fuzz` preset),
# Clang only, and never part of `all`, ctest, or the package. Run one with
# a corpus directory, e.g.
#
#   ASAN_OPTIONS=alloc_dealloc_mismatch=0 \
#   ./build/fuzz/fuzz/fuzz_session_snapshot tests/fixtures/session -max_total_time=60
#
# The ASAN_OPTIONS setting is needed for the three JSON harnesses: Qt's JSON
# parser sorts object keys with std::stable_sort, whose temporary buffer in
# GCC 16's libstdc++ is taken with operator new and returned with free, and
# the sanitizer reports that mismatch inside Qt on the first object it sees
# (docs/dependency-review.md). It is Qt's, not ours; every other check stays
# on. A crash, any other sanitizer report, or an assertion inside a harness
# is a finding.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "The fuzz harnesses need Clang's libFuzzer")
endif()

function(omanotes_add_fuzzer name)
    add_executable("${name}" EXCLUDE_FROM_ALL "${name}.cpp" ${ARGN})
    target_include_directories("${name}" PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries("${name}" PRIVATE Qt6::Widgets)
    target_compile_options("${name}" PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options("${name}" PRIVATE -fsanitize=fuzzer,address,undefined)
    omanotes_enable_warnings("${name}")
    omanotes_enable_hardening("${name}")
    add_dependencies(fuzzers "${name}")
endfunction()

add_custom_target(fuzzers)

omanotes_add_fuzzer(
    fuzz_session_snapshot
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
)
omanotes_add_fuzzer(
    fuzz_recovery_record
    "${CMAKE_SOURCE_DIR}/src/persistence/recovery_store.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/atomic_file_writer.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/conflict_detector.cpp"
    "${CMAKE_SOURCE_DIR}/src/persistence/note_reader.cpp"
    "${CMAKE_SOURCE_DIR}/src/session/session_snapshot.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
)
omanotes_add_fuzzer(
    fuzz_keymap
    "${CMAKE_SOURCE_DIR}/src/app/keymap.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/command_registry.cpp"
)
omanotes_add_fuzzer(
    fuzz_theme
    "${CMAKE_SOURCE_DIR}/src/ui/theme_adapter.cpp"
)
omanotes_add_fuzzer(
    fuzz_link
    "${CMAKE_SOURCE_DIR}/src/ui/markdown_view.cpp"
    "${CMAKE_SOURCE_DIR}/src/workspace/workspace_root.cpp"
)
