cmake_minimum_required(VERSION 3.28)
project(readfile LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

# Asynkron fil-I/O kräver io_uring: liburing-dev (sudo apt install liburing-dev)
# och BOOST_ASIO_HAS_IO_URING. Definieras bara här, så att övriga program
# fortsätter använda epoll för sina sockets.
find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED IMPORTED_TARGET liburing)

add_executable(readfile src/readfile.cxx)
target_link_libraries(readfile PRIVATE asio::common PkgConfig::URING)
target_compile_definitions(readfile PRIVATE BOOST_ASIO_HAS_IO_URING)
