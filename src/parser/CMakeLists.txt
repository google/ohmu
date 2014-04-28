cmake_minimum_required(VERSION 2.8)

add_library(parser
  Token.cpp
  Lexer.cpp
  DefaultLexer.cpp
  Parser.cpp
)
