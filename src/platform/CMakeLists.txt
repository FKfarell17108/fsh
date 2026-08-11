if(UNIX)
    set(FSH_PLATFORM_DIR posix)
else()
    message(FATAL_ERROR "fsh: no platform implementation available for this OS")
endif()

add_library(fsh_platform STATIC
    platform.h
    ${FSH_PLATFORM_DIR}/posix_process.c
    ${FSH_PLATFORM_DIR}/posix_pty.c
    ${FSH_PLATFORM_DIR}/posix_term.c
)

target_include_directories(fsh_platform PUBLIC ${CMAKE_SOURCE_DIR}/src)

if(UNIX)
    target_link_libraries(fsh_platform PUBLIC util)
endif()
