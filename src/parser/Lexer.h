//===- Lexer.h -------------------------------------------------*- C++ --*-===//
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
// This file defines a basic lexing infrastructure.
//
// class CharStream:
// class FileStream:         reads characters from a file.
// class InteractiveStream:  reads characters line by line from stdin.
//
// class Lexer: base class for custom lexers.
//
//===----------------------------------------------------------------------===//


#ifndef OHMU_LEXER_H
#define OHMU_LEXER_H

#include "parser/Token.h"

#include <stdio.h>

#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace ohmu {

namespace parsing {

// A stream of characters.
// Derived classes must override fillBuffer to read from the stream.
class CharStream {
public:
  virtual unsigned fillBuffer(char* buf, unsigned size) = 0;
  virtual ~CharStream() {}
};


// A converts a file to a stream of characters.
class FileStream : public CharStream {
public:
  FileStream(FILE* f) : file_(f) { }
  ~FileStream() { }

  virtual unsigned fillBuffer(char* buf, unsigned size);

private:
  FILE* file_;
};


#ifndef _MSC_VER
// Reads a stream of characters from standard input, using the readline library.
class InteractiveStream : public CharStream {
public:
  InteractiveStream(const char* prompt1, const char* prompt2);
  ~InteractiveStream() { }

  virtual unsigned fillBuffer(char* buf, unsigned size);

  inline void resetPrompt() { firstLine_ = true; }

private:
  static bool readlineLibraryInitialized_;
  const char* prompt1_;
  const char* prompt2_;
  bool firstLine_;
};
#endif


// Base class for lexers.
// Derived classes override readToken() to parse characters into Tokens.
class Lexer {
public:
  Lexer()
    : lineNum_(1), linePos_(1),
      buffer_(0), bufferLen_(0), bufferPos_(0),
      stream_eof_(true), lexical_error(false),
      tokenBuffer_(0), tokenPos_(0),
      charStream_(0), startKeywordTokenID_(TK_BasicTokenEnd),
      eofToken_(TK_EOF), emptyString_("") {
    buffer_      = new char[bufferCapacity_];
    tokenBuffer_ = new char[tokenCapacity_];
  }

  virtual ~Lexer() {
    if (buffer_)      delete[] buffer_;
    if (tokenBuffer_) delete[] tokenBuffer_;
  }

  // Defined by derived classes to register supported tokens
  virtual const char* getTokenIDString(unsigned tid) = 0;

  // Look up the token id for the token named s.
  virtual unsigned lookupTokenID(const std::string& s);

  // Defined by derived classes to parse tokens.
  virtual Token readToken() = 0;

  // Look up the token id for keyword s.  If s is not in the keyword
  // table, then register it as a new keyword and return the new id.
  virtual unsigned registerKeyword(const std::string& s);

  // Switch to a new character stream
  void setStream(CharStream *stream) {
    charStream_   = stream;
    lineNum_      = 1;
    linePos_      = 1;
    bufferLen_    = 0;
    bufferPos_    = 0;
    stream_eof_   = false;
    lexical_error = false;
    tokenPos_     = 0;
  }

  // Get the i'th lookahead token.
  const Token& look(unsigned i = 0) {
    unsigned lsize = lookAhead_.size();
    if (i >= lsize)
      readTokens(i - lsize + 1);
    return lookAhead_[i];
  }

  // Pull the next token off the token stream.
  void consume() {
    lookAhead_.pop_front();
  }

  // clear all unhandled input.
  void clearUnhandledInput() {
    lookAhead_.clear();
    bufferPos_ = bufferLen_;
  }

  // Return true if no more tokens are available.
  bool eof() const {
    return (stream_eof_ || lexical_error) && (lookAhead_.size() == 0);
  }

  // Must be called by derived classes to set the index of the last
  // built-in token id.
  void setKeywordStartID(unsigned tid) {
    startKeywordTokenID_ = tid;
  }

  unsigned getKeywordStartID() const {
    return startKeywordTokenID_;
  }

  unsigned getLastTokenID() const {
    return startKeywordTokenID_ + keyList_.size() - 1;
  }

  // Returns the token id of the given keyword
  unsigned lookupKeyword(const std::string& s) {
    KeywordDict::iterator it = keyDict_.find(s);
    if (it == keyDict_.end()) return 0;
    else return it->second + startKeywordTokenID_;
  }

  // Returns the keyword string for the given keyword token id.
  const std::string& lookupKeywordStr(unsigned k) {
    if (k < startKeywordTokenID_ ||
        k > (keyList_.size() + startKeywordTokenID_))
      return emptyString_;
    return keyList_[k - startKeywordTokenID_];
  }

protected:
  // Gets the i'th character of lookahead.
  char lookChar(unsigned i = 0) {
    unsigned bsize = bufferSize();
    if (i < bsize) {
      return getChar(i);
    }
    else {
      fillBuffer(i - bsize + 1);
      if (i < bufferSize()) return getChar(i);
    }
    return 0;
  }

  // Skips the current character.
  // A call to lookChar(0) must be done first, to ensure that a current
  // character exists.
  void skipChar() {
    if (bufferPos_ < bufferLen_) {
      ++bufferPos_;
    }
    ++linePos_;
  }

  // Puts the char into the current token buffer.
  // Returns true on success, or false if the token buffer is full.
  bool putChar(char c) {
    if (tokenPos_ < tokenCapacity_-1) {
      tokenBuffer_[tokenPos_] = c;
      ++tokenPos_;
      return true;
    }
    return false;
  }

  // Complete token, and return a reference to the string data.
  // This string must be copied before reading any further data.
  StringRef finishToken() {
    unsigned len = tokenPos_;
    tokenBuffer_[tokenPos_] = 0;   // null terminate
    tokenPos_ = 0;
    return StringRef(tokenBuffer_, len);
  }

  // Return true if there are no more chars in the character stream.
  bool stream_eof() {
    return stream_eof_ && (bufferSize() == 0);
  }

  // Return the number of enclosing braces
  unsigned getCurrentBraceNesting() { return braces_.size(); }

  // Get the current source location
  SourceLocation getCurrentLocation() {
    return SourceLocation(lineNum_, static_cast<unsigned short>(linePos_));
  }

  // Tell the lexer that an error has occured.
  void signalLexicalError();

  // Tell the lexer that a newline has been seen.
  void signalNewline() {
    ++lineNum_;
    linePos_ = 0;
  }

  // Tell the lexer that an open brace with the given id has been seen.
  void signalOpenBrace(unsigned short tokid) {
    braces_.push_back(tokid);
  }

  // Tell the lexer that a close brace has been seen.  The id should
  // be that of the corresponding open brace.
  bool signalCloseBrace(unsigned short tokid);

 private:
  static const unsigned bufferCapacity_  = 65536;
  static const unsigned tokenCapacity_   = 1024;

  // Current size of char buffer
  unsigned bufferSize()     const { return bufferLen_ - bufferPos_; }

  // Get character at position i from char buffer
  char  getChar(unsigned i) const { return buffer_[bufferPos_ + i]; }
  char& getChar(unsigned i)       { return buffer_[bufferPos_ + i]; }

  // Read at least numChars into the character buffer
  void fillBuffer(unsigned numChars);

  // Read numTokens into the lookahead buffer
  void readTokens(unsigned numTokens);

 private:
  unsigned  lineNum_;                    // current line number
  unsigned  linePos_;                    // current line position
  std::vector<unsigned short> braces_;   // stack for matching braces

  char*     buffer_;         // buffered input
  unsigned  bufferLen_;      // current buffer size (not capacity)
  unsigned  bufferPos_;      // current read position within buffer_

  bool      stream_eof_;     // true when we hit end of input
  bool      lexical_error;   // true when we hit a lexical error

  char*     tokenBuffer_;    // stores characters for the current token
  unsigned  tokenPos_;       // write position within tokenBuffer_

  CharStream*       charStream_;    // incoming character stream
  std::deque<Token> lookAhead_;     // queue of lexed tokens

  typedef std::map<std::string, unsigned> KeywordDict;
  typedef std::vector<std::string>        KeywordList;

  unsigned    startKeywordTokenID_;
  KeywordDict keyDict_;
  KeywordList keyList_;
  KeywordDict tokenDict_;
  KeywordList tokenList_;

  Token       eofToken_;
  std::string emptyString_;
};

} // end namespace parsing

} // end namespace ohmu

#endif

