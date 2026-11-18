add_library(xrNetServer)

target_sources(xrNetServer PRIVATE
    guids.cpp
    ip_filter.cpp
    ip_filter.h
    NET_AuthCheck.cpp
    NET_AuthCheck.h
    #NET_Client.cpp
    #NET_Client.h
    NET_Common.cpp
    NET_Common.h
    NET_Compressor.cpp
    NET_Compressor.h
    NET_Log.cpp
    NET_Log.h
    NET_Messages.h
    NET_PlayersMonitor.h
    #NET_Server.cpp
    #NET_Server.h
    NET_Shared.h
    stdafx.cpp
    stdafx.h
    enet/NET_Client.cpp
    enet/NET_Client.h
    enet/NET_ENet.cpp
    enet/NET_ENet.h
    enet/NET_Server.cpp
    enet/NET_Server.h
)

target_include_directories(xrNetServer
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/enet"
    "${XRAY_SOURCE_DIR}/src"
)

target_link_libraries(xrNetServer
    PRIVATE
    xrCore
    xrENet
)

target_compile_definitions(xrNetServer
    PRIVATE
    XR_NETSERVER_EXPORTS
)

set_target_properties(xrNetServer PROPERTIES
    PREFIX ""
)

target_precompile_headers(xrNetServer
    PRIVATE
    stdafx.h
)

if (BUILD_SHARED_LIBS)
    install(TARGETS xrNetServer LIBRARY
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
endif()
