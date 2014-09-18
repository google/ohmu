cmake_minimum_required(VERSION 2.8)

add_executable(test_BNFParser test_BNFParser.cpp)
target_link_libraries(test_BNFParser base parser)
add_dependencies(test_BNFParser parser_grammar)

add_executable(test_TILParser test_TILParser.cpp)
target_link_libraries(test_TILParser base parser ohmu_til tilpasslib)
add_dependencies(test_TILParser ohmu_grammar)

add_library(test_visitor test_visitor.cpp)

add_executable(test_llvm test_llvm.cpp)
target_link_libraries(test_llvm base parser ohmu_til tilpasslib
                      backend_llvm ${llvm_libs})
add_dependencies(test_llvm ohmu_grammar)
