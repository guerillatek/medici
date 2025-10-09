add_library(httpUtils SHARED 
    FieldContentUtils.cpp FieldContentUtils.hpp
    HttpFields.cpp HttpFields.hpp
    ContentType.cpp ContentType.hpp
    CompressionUtils.cpp CompressionUtils.hpp
    MultipartPayload.cpp MultipartPayload.hpp
)

target_compile_options(httpUtils PUBLIC -fPIC)
target_link_options(httpUtils PUBLIC "-Wl,--no-as-needed")
target_include_directories(httpUtils PRIVATE cpp)
target_include_directories(httpUtils PRIVATE 
../ 
../../)
