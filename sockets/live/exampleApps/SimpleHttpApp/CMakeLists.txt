add_executable(simpleHttpApp 
    main.cpp
)
target_include_directories(simpleHttpApp PUBLIC ../../../../)


set_target_properties(simpleHttpApp PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/examples)


target_link_libraries(simpleHttpApp PRIVATE
liveMediciSockets 
cryptpoUtils
httpUtils
brotlienc 
brotlidec
OpenSSL::SSL OpenSSL::Crypto 
ZLIB::ZLIB
)



