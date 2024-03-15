cmake_minimum_required(VERSION 3.16)
project(midi-link)

set(CMAKE_CXX_STANDARD 17)

include(external/link/AbletonLinkConfig.cmake)
add_subdirectory(external/rtmidi)

add_executable(midi-link main.cpp)

target_link_libraries(midi-link Ableton::Link rtmidi)