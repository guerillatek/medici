cmake_minimum_required(VERSION 3.20)
project(medici)

set(CMAKE_CXX_STANDARD 23)

# set(CMAKE_VERBOSE_MAKEFILE ON)
set(Boost_USE_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)


find_package(Boost CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_compile_options(-Werror=return-type
                        -Werror=return-type -Werror=unused-variable
                        -Werror=unused-function -Werror=unused-value
                        -Werror=unused-but-set-variable)

add_subdirectory(tests)
add_subdirectory(sockets)
add_subdirectory(http)
add_subdirectory(cryptoUtils)

