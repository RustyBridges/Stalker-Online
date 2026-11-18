# ENet (https://github.com/lsalzman/enet), used by xrNetServer as the
# cross-platform network transport. Mirrors the configure checks of ENet's
# own CMakeLists.txt.
add_library(xrENet STATIC)

set(ENET_DIR "${XRAY_SOURCE_DIR}/Externals/enet")

target_sources(xrENet PRIVATE
    "${ENET_DIR}/callbacks.c"
    "${ENET_DIR}/compress.c"
    "${ENET_DIR}/host.c"
    "${ENET_DIR}/list.c"
    "${ENET_DIR}/packet.c"
    "${ENET_DIR}/peer.c"
    "${ENET_DIR}/protocol.c"
    "${ENET_DIR}/unix.c"
    "${ENET_DIR}/win32.c"
)

target_include_directories(xrENet
    PUBLIC
    "${ENET_DIR}/include"
)

if (NOT WIN32)
    include(CheckFunctionExists)
    include(CheckStructHasMember)
    include(CheckTypeSize)
    check_function_exists("fcntl" HAS_FCNTL)
    check_function_exists("poll" HAS_POLL)
    check_function_exists("getaddrinfo" HAS_GETADDRINFO)
    check_function_exists("getnameinfo" HAS_GETNAMEINFO)
    check_function_exists("gethostbyname_r" HAS_GETHOSTBYNAME_R)
    check_function_exists("gethostbyaddr_r" HAS_GETHOSTBYADDR_R)
    check_function_exists("inet_pton" HAS_INET_PTON)
    check_function_exists("inet_ntop" HAS_INET_NTOP)
    check_struct_has_member("struct msghdr" "msg_flags" "sys/types.h;sys/socket.h" HAS_MSGHDR_FLAGS)
    set(CMAKE_EXTRA_INCLUDE_FILES "sys/types.h" "sys/socket.h")
    check_type_size("socklen_t" HAS_SOCKLEN_T BUILTIN_TYPES_ONLY)
    unset(CMAKE_EXTRA_INCLUDE_FILES)

    foreach (check
        HAS_FCNTL HAS_POLL HAS_GETADDRINFO HAS_GETNAMEINFO
        HAS_GETHOSTBYNAME_R HAS_GETHOSTBYADDR_R HAS_INET_PTON HAS_INET_NTOP
        HAS_MSGHDR_FLAGS HAS_SOCKLEN_T)
        if (${check})
            target_compile_definitions(xrENet PRIVATE ${check}=1)
        endif()
    endforeach()
else()
    target_link_libraries(xrENet PUBLIC ws2_32 winmm)
endif()

set_target_properties(xrENet PROPERTIES
    PREFIX ""
    POSITION_INDEPENDENT_CODE ON
)
