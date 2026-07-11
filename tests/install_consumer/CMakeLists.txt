cmake_minimum_required(VERSION 3.15)
project(ThimbleInstallConsumer LANGUAGES CXX)
enable_testing()

find_package(Thimble 0.1 REQUIRED CONFIG)
add_executable(thimble_install_consumer main.cpp)
target_link_libraries(thimble_install_consumer PRIVATE Thimble::thimble)
add_test(NAME thimble_install_consumer COMMAND thimble_install_consumer)
