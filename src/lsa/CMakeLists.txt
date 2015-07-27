add_library(lsa
  BuildCallGraph.cpp
)

# There is quite some repetition here, but it does not as simple to remove
# dependencies as it may appear.
target_link_libraries(lsa
  clangASTMatchers
  clangTooling
  clangDriver
  clangAST
  clangLex
  clangParse
  clangBasic
  clangFrontend
  clangEdit
  clangAST
  clangSerialization
  clangSema
  clangAST
  clangAnalysis
  clangASTMatchers
  clangFrontendTool
  clangDynamicASTMatchers
  clangTooling
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangLex
  clangRewrite
  clangTooling
  clangToolingCore
  
  ohmuTil

  LLVMMC
  LLVMMCParser
  LLVMIRReader
  LLVMOption
  LLVMCore
  LLVMSupport
)