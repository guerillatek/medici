find_package(unofficial-brotli CONFIG QUIET)
if(TARGET unofficial::brotli::brotlienc)
    set(BROTLI_ENC_LIBRARY unofficial::brotli::brotlienc)
    set(BROTLI_DEC_LIBRARY unofficial::brotli::brotlidec)
else()
    # Fall back to a system-installed brotli when the vcpkg port isn't available
    set(BROTLI_ENC_LIBRARY brotlienc)
    set(BROTLI_DEC_LIBRARY brotlidec)
endif()

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
${BROTLI_ENC_LIBRARY}
${BROTLI_DEC_LIBRARY}
OpenSSL::SSL OpenSSL::Crypto 
ZLIB::ZLIB
)



