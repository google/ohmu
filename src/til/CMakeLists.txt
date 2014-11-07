cmake_minimum_required(VERSION 2.8)

add_library(til STATIC
  CFGBuilder.cpp
  CFGReducer.cpp
  CopyReducer.cpp
  Global.cpp
  InplaceReducer.cpp
  Scope.cpp
  SSAPass.cpp
  TIL.cpp
)

target_link_libraries(til base)
