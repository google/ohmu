cmake_minimum_required(VERSION 2.8)

add_custom_target(parser_grammar)
add_custom_command(
    TARGET  parser_grammar
    DEPENDS parser.grammar
    #OUTPUT  parser.grammar
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/parser.grammar ${CMAKE_CURRENT_BINARY_DIR}/parser.grammar)
