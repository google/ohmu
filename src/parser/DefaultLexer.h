//===- DefaultLexer.h ------------------------------------------*- C++ --*-===//
// Copyright 2014  Google
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//===----------------------------------------------------------------------===//
//
// This file defines the default lexer for ohmu.  Tokens consist of:
//
// identifers:   x, y, foobar
// operators:    +, <<, %^&=   (any sequence of symbols, excluding punctuation)
// booleans:     true, false
// integers:     0, 324, 0x32FF
// floats:       1.52, 1.2e+6
// characters:   'a', 'z'
// strings:      "Hello World!\n"
// punctuation:  ( ) [ ] { } , ; : .
//
// Keywords overlap with identifiers and symbols, and must be registered.
//
//===----------------------------------------------------------------------===//


#ifndef OHMU_DEFAULTLEXER_H
#define OHMU_DEFAULTLEXER_H

#include "base/MemRegion.h"

#include "parser/Token.h"
#include "parser/Lexer.h"


namespace ohmu {
namespace parsing {


enum DefaultTokenIDs {
  TK_Identifier = TK_BasicTokenEnd,
  TK_Operator,

  TK_LitCharacter,
  TK_LitInteger,
  TK_LitFloat,
  TK_LitString,

  TK_LParen,
  TK_RParen,
  TK_LCurlyBrace,
  TK_RCurlyBrace,
  TK_LSquareBrace,
  TK_RSquareBrace,
  TK_Comma,
  TK_Semicolon,
  TK_Colon,
  TK_Period,

  TK_BeginKeywordIDs
};


class DefaultLexer : public Lexer {
public:
  DefaultLexer() : interactive_(false) {
    setKeywordStartID(TK_BeginKeywordIDs);
    stringArena_.setRegion(&stringRegion_);
  }
  DefaultLexer(const DefaultLexer& l) = delete;

  void readNewline(char c);

  void readIdentifier(char startChar);
  void readInteger   (char startChar);
  void readHexInteger();
  void readOperator  (char startChar);

  void readLineComment();
  bool readEscapeCharacter(char c);
  bool readString();
  bool readCharacter();
  bool readFloatExp(char startChar);

  StringRef copyStr(StringRef s) {
    char* mem = static_cast<char*>(stringArena_.allocate(s.length()+1));
    return copyStringRef(mem, s);
  }

  virtual const char* getTokenIDString(unsigned tid);
  virtual unsigned    registerKeyword(const std::string& s);

  virtual Token readToken();

  inline bool isInteractive() const { return interactive_; }
  inline void setInteractive(bool b) { interactive_ = b; }

private:
  MemRegion    stringRegion_;   // Region to allocate all token strings
  MemRegionRef stringArena_;

  bool interactive_;
};

}  // end namespace parsing

}  // end namespace ohmu

#endif
