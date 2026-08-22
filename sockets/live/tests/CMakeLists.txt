


find_package(unofficial-brotli CONFIG QUIET)
if(TARGET unofficial::brotli::brotlienc)
    set(BROTLI_ENC_LIBRARY unofficial::brotli::brotlienc)
    set(BROTLI_DEC_LIBRARY unofficial::brotli::brotlidec)
else()
    # Fall back to a system-installed brotli when the vcpkg port isn't available
    set(BROTLI_ENC_LIBRARY brotlienc)
    set(BROTLI_DEC_LIBRARY brotlidec)
endif()

add_executable(LiveSocketsTests 
    compileTests.cpp
    TCPClientServerTests.cpp
    ServerClientTestHarness.cpp
    HttpServerClientTests.cpp
    WSServerClientTests.cpp
)
target_include_directories(LiveSocketsTests PUBLIC ../../../..)



set_target_properties(LiveSocketsTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/tests)


target_link_libraries(LiveSocketsTests 
boost_program_options 
boost_container 
boost_unit_test_framework 
OpenSSL::SSL OpenSSL::Crypto 
ZLIB::ZLIB
cryptpoUtils
httpUtils
ssl
crypto
z
${BROTLI_ENC_LIBRARY}
${BROTLI_DEC_LIBRARY}
liveMediciSockets
)



