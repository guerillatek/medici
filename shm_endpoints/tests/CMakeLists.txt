
file(GLOB_RECURSE MediciTESTSRC ./*.cpp)
add_executable(MediciShmEndpointInProcTests 
SharedMemEndpointTests.cpp)

target_include_directories(MediciShmEndpointInProcTests PRIVATE ../../..)

set_target_properties(MediciShmEndpointInProcTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/tests)

target_link_libraries(MediciShmEndpointInProcTests boost_program_options boost_container boost_unit_test_framework)


# Shared consumer process
add_executable(testConsumer consumerMain.cpp)
target_include_directories(testConsumer PRIVATE ../../..)
target_link_libraries(testConsumer)
set_target_properties(testConsumer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/tests)

add_executable(testProducer producerMain.cpp)
target_include_directories(testProducer PRIVATE ../../..)
target_link_libraries(testProducer)
set_target_properties(testProducer PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/tests)

add_executable(testDuplex duplexMain.cpp)
target_include_directories(testDuplex PRIVATE ../../..)
target_link_libraries(testDuplex)
set_target_properties(testDuplex PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/tests)


# Channel name validation test
add_executable(ShmUniqueNametest ShmUniqueNametest.cpp)
target_include_directories(ShmUniqueNametest PRIVATE ../../..)
target_link_libraries(ShmUniqueNametest pthread)
set_target_properties(ShmUniqueNametest PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/tests)

# Shared consumer process
add_executable(MediciShmEndpointOutProcTests ShmEndpointOutProcTest.cpp)
target_include_directories(MediciShmEndpointOutProcTests PRIVATE ../../..)
target_link_libraries(MediciShmEndpointOutProcTests boost_program_options boost_container boost_unit_test_framework)
set_target_properties(MediciShmEndpointOutProcTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/tests)


# Shared memory size and clear test
add_executable(ShmMemClearTests ShmMemClearTests.cpp)
target_include_directories(ShmMemClearTests PRIVATE ../../..)
target_link_libraries(ShmMemClearTests)
set_target_properties(ShmMemClearTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/tests)
