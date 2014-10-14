cmake_minimum_required(VERSION 2.8)

add_library(tilpasslib STATIC
  InplaceReducer.cpp
  CFGReducer.cpp
  SSAPass.cpp
)

target_link_libraries(tilpasslib base ohmu_til)
