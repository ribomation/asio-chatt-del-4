cmake_minimum_required(VERSION 3.28)
project(chatclient LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(chatclient src/chatclient.cxx)
target_link_libraries(chatclient PRIVATE asio::ssl)
