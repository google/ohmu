cmake_minimum_required(VERSION 2.8)

add_executable(test_base test_base.cpp)
target_link_libraries(test_base base)

add_executable(test_BNFParser test_BNFParser.cpp)
target_link_libraries(test_BNFParser base parser til)
add_dependencies(test_BNFParser parser_grammar)

add_executable(test_parser test_parser.cpp)
target_link_libraries(test_parser base parser til)
add_dependencies(test_parser ohmu_grammar)

add_library(test_visitor test_visitor.cpp)

#add_executable(test_llvm test_llvm.cpp)
#target_link_libraries(test_llvm base parser til backend_llvm ${llvm_libs})
#add_dependencies(test_llvm ohmu_grammar)
