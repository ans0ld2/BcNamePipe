cmake_minimum_required(VERSION 3.12.0)

project(BcNamedPipe VERSION 0.1.0)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(BcNamedPipe STATIC BcNamedPipe.cpp BcNamedPipe.h)

find_package(Threads REQUIRED)
target_link_libraries(BcNamedPipe PUBLIC Threads::Threads)

target_include_directories(BcNamedPipe PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
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

add_executable(bc_pipe_reader examples/reader.cpp)
target_link_libraries(bc_pipe_reader PRIVATE BcNamedPipe)
target_compile_features(bc_pipe_reader PRIVATE cxx_std_17)
set_target_properties(bc_pipe_reader PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_compile_options(bc_pipe_reader PRIVATE -Wall -Wextra -Wpedantic)

add_executable(bc_pipe_writer examples/writer.cpp)
target_link_libraries(bc_pipe_writer PRIVATE BcNamedPipe)
target_compile_features(bc_pipe_writer PRIVATE cxx_std_17)
set_target_properties(bc_pipe_writer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_compile_options(bc_pipe_writer PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME BcNamedPipeTest COMMAND BcNamedPipeTest)

install(TARGETS BcNamedPipe
    EXPORT BcNamedPipeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES BcNamedPipe.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT BcNamedPipeTargets
    FILE BcNamedPipeTargets.cmake
    NAMESPACE BcNamedPipe::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BcNamedPipe)

configure_package_config_file(
    cmake/BcNamedPipeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/BcNamedPipeConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BcNamedPipe)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/BcNamedPipeConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/BcNamedPipeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/BcNamedPipeConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BcNamedPipe)
