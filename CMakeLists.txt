cmake_minimum_required(VERSION 3.28)
project(asio-chatt-del-4 LANGUAGES CXX)

# Demoprogram till artikeln
#   "Bygg en chattserver med Boost.Asio, del 4: TLS, UDP och graceful shutdown"
# Bygg allt härifrån, eller ett program i taget från dess egen mapp.

include(cmake/common.cmake)

add_subdirectory(signals)
add_subdirectory(daytime-udp-server)
add_subdirectory(daytime-udp-client)
add_subdirectory(echoserver-tls)
add_subdirectory(readfile)
add_subdirectory(chatserver)
