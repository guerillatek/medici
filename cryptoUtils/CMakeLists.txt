add_library(cryptpoUtils SHARED 
    CryptoUtils.cpp CryptoUtils.hpp
    OpenSSLUtils.cpp CryptoUtils.hpp
)

target_include_directories(cryptpoUtils PRIVATE ../ts-core/core/cpp)
target_compile_options(cryptpoUtils PUBLIC -fPIC)
