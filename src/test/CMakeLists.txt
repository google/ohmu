cmake_minimum_required(VERSION 2.8)

add_executable(test_BNFParser test_BNFParser.cpp)
target_link_libraries(test_BNFParser base parser)

add_executable(test_TILParser test_TILParser.cpp)
target_link_libraries(test_TILParser base parser ohmu_til)
add_dependencies(test_TILParser src/grammar/ohmu.grammar)

