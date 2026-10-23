cmake_minimum_required(VERSION 3.12.0)

project(BcNamedPipe)

add_library(BcNamedPipe STATIC BcNamedPipe.cpp BcNamedPipe.h)

target_include_directories(BcNamedPipe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(BcNamedPipe PUBLIC cxx_std_17)
set_target_properties(BcNamedPipe PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_compile_options(BcNamedPipe PRIVATE -Wall -Wextra -Wpedantic)

add_executable(BcNamedPipeTest tests/BcNamedPipeTest.cpp)
target_link_libraries(BcNamedPipeTest PRIVATE BcNamedPipe)
target_compile_features(BcNamedPipeTest PRIVATE cxx_std_17)
set_target_properties(BcNamedPipeTest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_compile_options(BcNamedPipeTest PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME BcNamedPipeTest COMMAND BcNamedPipeTest)
