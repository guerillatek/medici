
file(GLOB_RECURSE MediciTESTSRC ./*.cpp)
add_executable(MediciEventQueueTests 
main.cpp
EventQueueTests.cpp)

target_include_directories(MediciEventQueueTests PRIVATE ../..)
target_include_directories(MediciEventQueueTests PRIVATE ../../ts-core/core/cpp)

set_target_properties(MediciEventQueueTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_CURRENT_BINARY_DIR}/tests)

target_link_libraries(MediciEventQueueTests boost_program_options boost_container boost_unit_test_framework)
