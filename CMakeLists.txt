cmake_minimum_required(VERSION 3.12.0)

project(BcNamedPipe)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(BcNamedPipe STATIC BcNamedPipe.cpp BcNamedPipe.h)

target_include_directories(BcNamedPipe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})