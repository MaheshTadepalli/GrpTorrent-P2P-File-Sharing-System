cmake_minimum_required(VERSION 3.10)
project(grptorrent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(grptorrent
  src/grptorrent.cpp
)

if (WIN32)
  target_link_libraries(grptorrent ws2_32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(grptorrent Threads::Threads)
endif()

