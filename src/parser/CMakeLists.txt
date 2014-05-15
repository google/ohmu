cmake_minimum_required(VERSION 2.8)

# find_package(Readline REQUIRED)

add_library(parser STATIC
  Token.cpp
  Lexer.cpp
  DefaultLexer.cpp
  Parser.cpp
  ASTNode.cpp
  BNFParser.cpp
  TILParser.cpp
)

target_link_libraries(parser base)
target_link_libraries(parser readline)
