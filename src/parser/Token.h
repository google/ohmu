//===- Token.h -------------------------------------------------*- C++ --*-===//
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
// Defines classes for dealing with source files and tokens.
//
// class SourceLocation: a location within a source file.
// class Token: consists of a token id, the parsed string, and a SourceLocation.
// class TokenSet: a set of token ids.  Used in parsing.
//
//===----------------------------------------------------------------------===//


#ifndef OHMU_TOKEN_H
#define OHMU_TOKEN_H

#include "base/LLVMDependencies.h"

namespace ohmu {

namespace parsing {

enum BasicTokenID {
  TK_None = 0,
  TK_EOF,
  TK_Error,
  TK_Newline,
  TK_Whitespace,
  TK_Comment,
  TK_BasicTokenEnd
};


struct SourceLocation {
  unsigned lineNum;
  unsigned short linePos;
  unsigned short fileIndex;

  SourceLocation()
    : lineNum(0), linePos(0), fileIndex(0)
  { }
  SourceLocation(unsigned ln, unsigned short lp, unsigned short fi=0)
    : lineNum(ln), linePos(lp), fileIndex(fi)
  { }
};


class Token {
public:
  Token()
    : tokenID_(TK_EOF), tokenStr_("")
  { }
  Token(const Token& tok)
    : tokenID_(tok.tokenID_), tokenStr_(tok.tokenStr_),
      sourceLoc_(tok.sourceLoc_)
  { }
  Token(unsigned short tid)
    : tokenID_(tid), tokenStr_(""), sourceLoc_(SourceLocation())
  { }
  Token(unsigned short tid, const char* s, const SourceLocation& loc)
    : tokenID_(tid), tokenStr_(StringRef(s)), sourceLoc_(SourceLocation())
  { }
  Token(unsigned short tid, StringRef s, const SourceLocation& loc)
    : tokenID_(tid), tokenStr_(s), sourceLoc_(loc)
  { }

  unsigned       id()        const { return tokenID_;  }
  unsigned       length()    const { return tokenStr_.size(); }
  StringRef      string()    const { return tokenStr_;  }
  SourceLocation location()  const { return sourceLoc_; }

  std::string cppString() const {
    return std::string(tokenStr_.c_str(), tokenStr_.size());
  }

  const char* c_str() const { return tokenStr_.c_str();  }

private:
  unsigned short tokenID_;
  StringRef      tokenStr_;
  SourceLocation sourceLoc_;
};


class TokenSet {
public:
  bool get(int i) const {
    unsigned idx = i/sizeof(unsigned);
    unsigned rem = i - idx*sizeof(unsigned);
    return (bits_[idx] >> rem) & 0x01;
  }

  void set(int i) {
    unsigned idx = i/sizeof(unsigned);
    unsigned rem = i - idx*sizeof(unsigned);
    bits_[idx] |= 0x01 << rem;
  }

  static void makeZero(TokenSet& tset) {
    for (unsigned i = 0; i < maxSize; ++i) tset.bits_[i] = 0;
  }

  static void makeUnion(TokenSet& set1, TokenSet& set2,
                        TokenSet& result)
  {
    for (unsigned i = 0; i < maxSize; ++i)
      result.bits_[i] = set1.bits_[i] | set2.bits_[i];
  }

private:
  static const unsigned maxSize = 16;

  unsigned bits_[maxSize];   // 16*32 token types should be enough...
};


}  // end namespace lexing
}  // end namespace ohmu

#endif

