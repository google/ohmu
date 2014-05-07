cmake_minimum_required(VERSION 2.8)

add_executable(test_BNFParser test_BNFParser.cpp)

target_link_libraries(test_BNFParser parser)
