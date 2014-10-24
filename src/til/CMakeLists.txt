cmake_minimum_required(VERSION 2.8)

add_library(tilpasslib STATIC
  CFGBuilder.cpp
  CFGReducer.cpp
  CopyReducer.cpp
  InplaceReducer.cpp
  Scope.cpp
  SSAPass.cpp
)

target_link_libraries(tilpasslib base ohmu_til)
