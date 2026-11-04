cmake_minimum_required(VERSION 3.28)
project(daytime-udp-client LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(daytime-udp-client src/daytime-udp-client.cxx)
target_link_libraries(daytime-udp-client PRIVATE asio::common)
