cmake_minimum_required(VERSION 2.8)

add_executable(x64builder x64builder.cpp)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  add_definitions(/Dsnprintf=sprintf_s)
endif()
