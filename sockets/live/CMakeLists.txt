
file(GLOB_RECURSE TCP_SRC TcpIp/*.cpp)
file(GLOB_RECURSE UDP_SRC Udp/*.cpp)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)


add_library(liveMediciSockets SHARED 
    IPEndpointConnectionManager.cpp IPEndpointConnectionManager.hpp
    IPEndpointPollManager.cpp IPEndpointPollManager.hpp
)

target_compile_options(liveMediciSockets PUBLIC -fPIC)
target_link_options(liveMediciSockets PUBLIC "-Wl,--no-as-needed" )
target_include_directories(liveMediciSockets PUBLIC ../../../ts-gw-base/gw/cpp)
target_include_directories(liveMediciSockets PUBLIC ../../..)



add_subdirectory(tests)