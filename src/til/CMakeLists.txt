cmake_minimum_required(VERSION 2.8)

add_library(tilpasslib STATIC
  CFGReducer.cpp
)

target_link_libraries(tilpasslib base ohmu_til)
