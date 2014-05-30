//===- Lexer.cpp -----------------------------------------------*- C++ --*-===//
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

#include <cstdlib>
#include <iostream>

#ifndef _MSC_VER
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "Lexer.h"

namespace ohmu {

namespace parsing {

unsigned FileStream::fillBuffer(char* buf, unsigned size) {
  size_t n = fread(buf, 1, size, file_);
  return static_cast<unsigned>(n);
}


bool InteractiveStream::readlineLibraryInitialized_ = false;

#ifndef _MSC_VER
InteractiveStream::InteractiveStream(const char* p1, const char* p2)
    : prompt1_(p1), prompt2_(p2), firstLine_(true) {
  if (!readlineLibraryInitialized_) {
    // Do nothing for now...
    readlineLibraryInitialized_ = true;
  }
}


unsigned InteractiveStream::fillBuffer(char* buf, unsigned size) {
  const char* p = firstLine_ ? prompt1_ : prompt2_;

  char* line = readline(p);
  if (line == 0)
    return 0;

  unsigned i = 0;
  while (i < size && line[i] != 0) {
    buf[i] = line[i];
    ++i;
  }
  if (i+1 < size) {
    buf[i]   = '\n';  // add on a newline.
    buf[i+1] = ' ';   // add whitespace to satisfy lexer lookahead.
    i += 2;
  }
  free(line);
  firstLine_ = false;
  return i;
}
#endif


// Look up the token id for the token named s.
unsigned Lexer::lookupTokenID(const std::string& s) {
  // initialize token dictionary on first call
  if (tokenList_.size() == 0) {
    for (unsigned i=0,n=getKeywordStartID(); i<n; ++i) {
      const char* cs = getTokenIDString(i);
      tokenList_.push_back(cs);
      tokenDict_[cs] = i;
    }
  }

  KeywordDict::iterator it = tokenDict_.find(s);
  if (it == tokenDict_.end()) return 0;
  return it->second;
}


unsigned Lexer::registerKeyword(const std::string& s) {
  KeywordDict::iterator it = keyDict_.find(s);
  if (it == keyDict_.end()) {
    unsigned sz = keyList_.size();
    keyList_.push_back(s);  // map from unsigned to string
    keyDict_[s] = sz;       // map from string to unsigned
    return sz + startKeywordTokenID_;
  }
  return it->second + startKeywordTokenID_;
}


void Lexer::signalLexicalError() {
  char c = lookChar();
  std::cerr << "Lexical error: unknown character ";
  if (c) std::cerr << "'" << c << "'";
  else   std::cerr << 0;
  lexical_error = true;
}


// Tell the lexer that a close brace has been seen.  The id should
// be that of the corresponding open brace.
bool Lexer::signalCloseBrace(unsigned short tokid) {
  if (braces_.size() > 0 && braces_.back() == tokid) {
    braces_.pop_back();
    return true;
  }
  // attempt to recover by popping extra braces off the stack
  while (braces_.size() > 0 && braces_.back() != tokid)
    braces_.pop_back();
  if (braces_.size() > 0)
    braces_.pop_back();     // pop off the given brace
  return false;
}


void Lexer::readTokens(unsigned numTokens) {
  unsigned i = 0;
  for (; i < numTokens; ++i) {
    if (stream_eof_ || lexical_error)
      break;
    lookAhead_.push_back(readToken());
  }

  // push extra EOF tokens onto the end if necessary to enable
  // unlimited lookahead.
  for (; i < numTokens; ++i) {
    lookAhead_.push_back(eofToken_);
  }
}


void Lexer::fillBuffer(unsigned numChars) {
  unsigned bsize = bufferSize();

  if (bufferPos_ > 0) {
    // Move unread characters to begining of buffer.
    // There should only be a few.
    if (bufferPos_ > bsize) {
      memcpy(buffer_, buffer_ + bufferPos_, bsize);
    }
    else {
      // regions overlap -- move the data byte by byte.
      char* p = buffer_ + bufferPos_;
      for (unsigned i = 0; i < bsize; ++i, ++p)
        buffer_[i] = *p;
    }

    bufferPos_ = 0;
    bufferLen_ = bsize;
  }

  // Sanity check.
  if (numChars + bufferLen_ > bufferCapacity_-1)
    numChars = bufferCapacity_ - bufferLen_ - 1;

  unsigned read = 0;
  while (read < numChars && !stream_eof_) {
    unsigned nread =
      charStream_->fillBuffer(buffer_ + bufferLen_,
                              bufferCapacity_ - bufferLen_);
    if (nread == 0) {
      stream_eof_ = true;
      break;
    }

    read += nread;
    bufferLen_ += nread;
  }

  // 0-pad any requested lookahead past end of file
  while (read < numChars) {
    buffer_[bufferLen_] = 0;
    ++bufferLen_;
    ++read;
  }
}

} // end namespace parsing

} // end namespace ohmu
