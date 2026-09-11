function(omanotes_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            "${target}"
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
                -Wnon-virtual-dtor
                -Wold-style-cast
                -Wcast-align
                -Woverloaded-virtual
                -Wnull-dereference
                -Wdouble-promotion
                -Wformat=2
                -Werror
        )
    else()
        message(FATAL_ERROR "Warnings-as-errors are only defined for Clang and GCC")
    endif()
endfunction()

# Compiler and linker hardening for every target (docs/threat-model.md, F-8).
#
#   -fstack-protector-strong   canaries on frames with arrays or address-taken locals
#   -fstack-clash-protection   probe large stack allocations page by page
#   -fcf-protection=full       CET: indirect-branch tracking and shadow stack marks
#   -fPIE / -pie               position independent, so ASLR applies to the text
#   -z relro -z now            GOT read-only after immediate binding (full RELRO)
#   -z noexecstack             no executable stack
#   _GLIBCXX_ASSERTIONS        bounds and precondition checks in the standard library
#   _FORTIFY_SOURCE=3          checked libc calls; needs optimisation, so not in Debug
#
# scripts/security-check.sh verifies what leaves a mark in the ELF. Stack
# clash protection leaves none; it is trusted from this file.
function(omanotes_enable_hardening target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            "${target}"
            PRIVATE
                -fstack-protector-strong
                -fstack-clash-protection
                -fcf-protection=full
                -fPIE
        )
        target_compile_definitions(
            "${target}"
            PRIVATE _GLIBCXX_ASSERTIONS "$<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=3>"
        )
        target_link_options("${target}" PRIVATE -pie -Wl,-z,relro,-z,now,-z,noexecstack)
    else()
        message(FATAL_ERROR "Hardening flags are only defined for Clang and GCC")
    endif()
endfunction()
