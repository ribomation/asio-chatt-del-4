cmake_minimum_required(VERSION 3.28)
project(signals LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(signals src/signals.cxx)
target_link_libraries(signals PRIVATE asio::common)
