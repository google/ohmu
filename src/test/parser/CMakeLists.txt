add_executable(test_BNFParser test_BNFParser.cpp)
target_link_libraries(test_BNFParser base parser til)
add_dependencies(test_BNFParser parser_grammar)

add_executable(test_parser test_parser.cpp)
target_link_libraries(test_parser parser til)
add_dependencies(test_parser ohmu_grammar)