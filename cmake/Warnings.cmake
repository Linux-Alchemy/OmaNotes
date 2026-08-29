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
    endif()
endfunction()

function(omanotes_enable_hardening target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options("${target}" PRIVATE -fstack-protector-strong -fPIE)
        target_link_options("${target}" PRIVATE -pie -Wl,-z,relro,-z,now,-z,noexecstack)
    endif()
endfunction()
