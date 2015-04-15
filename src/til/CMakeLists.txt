cmake_minimum_required(VERSION 2.8)

add_library(til STATIC
  Bytecode.cpp
  CFGBuilder.cpp
  Global.cpp
  SSAPass.cpp
  TIL.cpp
  TypedEvaluator.cpp
)

target_link_libraries(til base)
