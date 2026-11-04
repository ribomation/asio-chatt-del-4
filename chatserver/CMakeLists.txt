cmake_minimum_required(VERSION 3.28)
project(chatserver LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(chatserver src/chatserver.cxx)
target_link_libraries(chatserver PRIVATE asio::ssl)
