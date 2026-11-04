cmake_minimum_required(VERSION 3.28)
project(daytime-udp-server LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(daytime-udp-server src/daytime-udp-server.cxx)
target_link_libraries(daytime-udp-server PRIVATE asio::common)
