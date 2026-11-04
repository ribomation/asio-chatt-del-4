cmake_minimum_required(VERSION 3.28)
project(echoserver-tls LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common.cmake)

add_executable(echoserver-tls src/echoserver-tls.cxx)
target_link_libraries(echoserver-tls PRIVATE asio::ssl)
