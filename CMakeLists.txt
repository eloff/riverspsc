cmake_minimum_required(VERSION 3.6)
project(riverq)

set(CMAKE_CXX_COMPILER /usr/bin/c++)
set(CMAKE_C_COMPILER /usr/bin/cc)
set(CMAKE_LINKER /usr/bin/ld.gold)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES main.cc riverq.cc)
add_executable(riverq ${SOURCE_FILES})
target_link_libraries(riverq pthread)