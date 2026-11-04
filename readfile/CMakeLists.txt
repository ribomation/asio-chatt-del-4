cmake_minimum_required(VERSION 3.28)
project(readfile LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

# Asynkron fil-I/O kräver io_uring: liburing-dev (sudo apt install liburing-dev)
# och BOOST_ASIO_HAS_IO_URING. Definieras bara här, så att övriga program
# fortsätter använda epoll för sina sockets.
#
# BOOST_ASIO_HAS_IO_URING_AS_DEFAULT gör io_uring till reaktor för hela
# programmet, i stället för epoll med io_uring vid sidan om. Utan den hänger
# Boost 1.92 när fler än 128 completions blir klara i en klump (eventfd:n är
# edge-triggered och Asio tömmer bara 128 åt gången), vilket sker vid ett
# par hundra filer. Se README, Fördjupning.
find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED IMPORTED_TARGET liburing)

add_executable(readfile src/readfile.cxx)
target_link_libraries(readfile PRIVATE asio::common PkgConfig::URING)
target_compile_definitions(readfile PRIVATE BOOST_ASIO_HAS_IO_URING BOOST_ASIO_HAS_IO_URING_AS_DEFAULT)
