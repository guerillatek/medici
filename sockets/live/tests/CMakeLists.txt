


add_executable(LiveSocketsTests 
    compileTests.cpp
    clientServerTests.cpp
    writeStringToTempFile.cpp
    ServerClientTestHarness.cpp
    HttpServerClientTests.cpp
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
liveMediciSockets 
cryptpoUtils
httpUtils
brotlienc 
brotlidec
)



