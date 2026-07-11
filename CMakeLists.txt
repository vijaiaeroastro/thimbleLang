cmake_minimum_required(VERSION 3.15)
project(Thimble VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(thimble INTERFACE)
add_library(Thimble::thimble ALIAS thimble)
target_compile_features(thimble INTERFACE cxx_std_17)
target_include_directories(thimble INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

option(THIMBLE_BUILD_TESTS "Build Thimble tests" ON)
option(THIMBLE_BUILD_EXAMPLES "Build Thimble examples" ON)
option(THIMBLE_BUILD_BENCHMARKS "Build Thimble benchmarks" OFF)
option(THIMBLE_BUILD_FUZZER "Build the Clang libFuzzer target" OFF)

if(THIMBLE_BUILD_TESTS)
  enable_testing()
  add_executable(thimble_tests tests/test_language.cpp)
  target_link_libraries(thimble_tests PRIVATE Thimble::thimble)
  add_test(NAME thimble_language COMMAND thimble_tests)
  foreach(test_name IN ITEMS diagnostics host_boundaries limits_and_cycles)
    add_executable(thimble_${test_name} tests/test_${test_name}.cpp)
    target_link_libraries(thimble_${test_name} PRIVATE Thimble::thimble)
    add_test(NAME thimble_${test_name} COMMAND thimble_${test_name})
  endforeach()
  add_executable(thimble_smoke tests/smoke.cpp)
  target_link_libraries(thimble_smoke PRIVATE Thimble::thimble)
  add_test(NAME thimble_smoke COMMAND thimble_smoke)
  add_executable(thimble_parser_fuzz_smoke tests/parser_fuzz_smoke.cpp)
  target_link_libraries(thimble_parser_fuzz_smoke PRIVATE Thimble::thimble)
  add_test(NAME thimble_parser_fuzz_smoke COMMAND thimble_parser_fuzz_smoke)
endif()

if(THIMBLE_BUILD_FUZZER)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "THIMBLE_BUILD_FUZZER requires Clang")
  endif()
  add_executable(thimble_parser_fuzzer tests/parser_fuzzer.cpp)
  target_link_libraries(thimble_parser_fuzzer PRIVATE Thimble::thimble)
  target_compile_options(thimble_parser_fuzzer PRIVATE
    -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
  target_link_options(thimble_parser_fuzzer PRIVATE
    -fsanitize=fuzzer,address,undefined)
endif()

if(THIMBLE_BUILD_EXAMPLES)
  add_executable(thimble_geometry examples/geometry_runtime.cpp)
  target_link_libraries(thimble_geometry PRIVATE Thimble::thimble)
  add_executable(thimble_policy examples/policy_runtime.cpp)
  target_link_libraries(thimble_policy PRIVATE Thimble::thimble)
endif()

if(THIMBLE_BUILD_BENCHMARKS)
  add_executable(thimble_benchmark benchmarks/runtime_benchmark.cpp)
  target_link_libraries(thimble_benchmark PRIVATE Thimble::thimble)
endif()

install(TARGETS thimble EXPORT ThimbleTargets)
install(DIRECTORY include/thimble DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ThimbleTargets NAMESPACE Thimble::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thimble)

configure_package_config_file(
  cmake/ThimbleConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ThimbleConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thimble)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ThimbleConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ThimbleConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ThimbleConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thimble)
