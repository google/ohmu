//===- DefaultLexer.cpp ----------------------------------------*- C++ --*-===//
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

#include "DefaultLexer.h"

namespace ohmu {

namespace parsing {

const char* DefaultLexer::getTokenIDString(unsigned tid) {
  switch (tid) {
    case TK_EOF:          return "TK_EOF";
    case TK_Error:        return "TK_Error";
    case TK_Newline:      return "TK_Newline";
    case TK_Whitespace:   return "TK_Whitespace";
    case TK_Comment:      return "TK_Comment";

    case TK_Identifier:   return "TK_Identifier";
    case TK_Operator:     return "TK_Operator";

    case TK_LitCharacter: return "TK_LitCharacter";
    case TK_LitInteger:   return "TK_LitInteger";
    case TK_LitFloat:     return "TK_LitFloat";
    case TK_LitString:    return "TK_LitString";

    case TK_LParen:       return "(";
    case TK_RParen:       return ")";
    case TK_LCurlyBrace:  return "{";
    case TK_RCurlyBrace:  return "}";
    case TK_LSquareBrace: return "[";
    case TK_RSquareBrace: return "]";
    case TK_Comma:        return ",";
    case TK_Semicolon:    return ";";
    case TK_Colon:        return ":";
    case TK_Period:       return ".";

    default: return lookupKeywordStr(tid).c_str();
  }
}

unsigned DefaultLexer::registerKeyword(const std::string& s) {
  if (s.length() == 1) {
    // See if the keyword corresponds to built-in punctuation.
    char c = s[0];
    if (c == '(') return TK_LParen;
    if (c == ')') return TK_RParen;
    if (c == '{') return TK_LCurlyBrace;
    if (c == '}') return TK_RCurlyBrace;
    if (c == '[') return TK_LSquareBrace;
    if (c == ']') return TK_RSquareBrace;
    if (c == ',') return TK_Comma;
    if (c == ';') return TK_Semicolon;
    if (c == ':') return TK_Colon;
    if (c == '.') return TK_Period;
  }
  return Lexer::registerKeyword(s);
}


inline bool isDigit(char c) {

  return (c >= '0' && c <= '9');
}

inline bool isHexDigit(char c) {
  return isDigit(c) ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

inline bool isLetter(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c == '_');
}

inline bool isWhiteSpace(char c) {
  return (c == ' ' || c == '\t');
}

inline bool isNewline(char c) {
  return (c == '\n' || c == '\r');
}

inline bool isOperatorChar(char c) {
  switch (c) {
    case '~': return true;
    case '!': return true;
    case '@': return true;
    case '#': return true;
    case '$': return true;
    case '%': return true;
    case '^': return true;
    case '&': return true;
    case '*': return true;
    case '-': return true;
    case '+': return true;
    case '=': return true;
    case '|': return true;
    case '<': return true;
    case '>': return true;
    case '?': return true;
    case '/': return true;
    case ':': return true;
    case '\\': return true;
    default:
      return false;
  }
}

void DefaultLexer::readNewline(char c) {
  if (c == '\n') {
    skipChar();
    if (lookChar() == '\r') skipChar();
  }
  if (c == '\r') {
    skipChar();
    if (lookChar() == '\n') skipChar();
  }
  signalNewline();
}


void DefaultLexer::readIdentifier(char startChar) {
  char c = startChar;
  putChar(c);
  skipChar();
  c = lookChar();
  while (isLetter(c) | isDigit(c)) {
    putChar(c);
    skipChar();
    c = lookChar();
  };
}


void DefaultLexer::readInteger(char startChar) {
  char c = startChar;
  do {
    putChar(c);
    skipChar();
    c = lookChar();
  } while (isDigit(c));
}


void DefaultLexer::readOperator(char startChar) {
  char c = startChar;
  do {
    putChar(c);
    skipChar();
    c = lookChar();
  } while (isOperatorChar(c));
}


void DefaultLexer::readLineComment() {
  skipChar();  // skip '/'
  skipChar();  // skip '/'

  char c = lookChar();
  while (c && !isNewline(c)) {
    skipChar();
    c = lookChar();
  }
  if (isNewline(c)) readNewline(c);
}


bool DefaultLexer::readEscapeCharacter(char c) {
  // translate string contents on the fly.
  if (c == 0) {
    signalLexicalError();
    return false;
  }
  if (c == '\n' || c == '\r' || c == '\t') {
    signalLexicalError();
    return false;
  }
  if (c == '\\') {
    skipChar();
    c = lookChar();
    switch (c) {
      case 0:
        signalLexicalError();
        return false;
      case 'n':
        putChar('\n');
        break;
      case 'r':
        putChar('\r');
        break;
      case 't':
        putChar('\t');
        break;
      default:
        putChar(c);
        break;
    }
  }
  else {
    putChar(c);
  }
  skipChar();
  return true;
}


bool DefaultLexer::readString() {
  skipChar();  // skip leading '"'
  char c = lookChar();
  while (c != '\"') {
    if (!readEscapeCharacter(c))
      return false;
    c = lookChar();
  }
  skipChar();  // skip trailing '"'
  return true;
}


bool DefaultLexer::readCharacter() {
  skipChar();  // skip leading '''
  char c = lookChar();
  while (c != '\'') {
    if (!readEscapeCharacter(c))
      return false;
    c = lookChar();
  }
  skipChar();  // skip trailing '''
  return true;
}


Token DefaultLexer::readToken() {
  char c = lookChar();

  while (true) {
    // skip whitespace
    while (isWhiteSpace(c)) {
      skipChar();
      c = lookChar();
    }

    // newlines
    if (isNewline(c)) {
      readNewline(c);
      if (interactive_ && getCurrentBraceNesting() == 0)
        return Token(TK_Newline);
      c = lookChar();
      continue;
    }

    // treat line comments as newlines
    if (c == '/' && lookChar(1) == '/') {
      readLineComment();
      c = lookChar();
      continue;
    }
    break;
  }

  SourceLocation sloc = getCurrentLocation();

  // punctuation
  if (c == '(') {
    skipChar();
    signalOpenBrace(TK_LParen);
    return Token(TK_LParen, "(", sloc);
  }
  if (c == ')') {
    skipChar();
    signalCloseBrace(TK_LParen);
    return Token(TK_RParen, ")", sloc);
  }
  if (c == '{') {
    skipChar();
    signalOpenBrace(TK_LCurlyBrace);
    return Token(TK_LCurlyBrace, "{", sloc);
  }
  if (c == '}') {
    skipChar();
    signalCloseBrace(TK_LCurlyBrace);
    return Token(TK_RCurlyBrace, "}", sloc);
  }
  if (c == '[') {
    skipChar();
    signalOpenBrace(TK_LSquareBrace);
    return Token(TK_LSquareBrace, "[", sloc);
  }
  if (c == ']') {
    skipChar();
    signalCloseBrace(TK_LSquareBrace);
    return Token(TK_RSquareBrace, "]", sloc);
  }
  if (c == ',') {
    skipChar();
    return Token(TK_Comma, ",", sloc);
  }
  if (c == ';') {
    skipChar();
    return Token(TK_Semicolon, ";", sloc);
  }
  if (c == ':' && !isOperatorChar(lookChar(1))) {
    skipChar();
    return Token(TK_Colon, ":", sloc);
  }
  if (c == '.') {
    skipChar();
    return Token(TK_Period, ".", sloc);
  }

  // identifiers
  if (isLetter(c)) {
    readIdentifier(c);
    StringRef str = copyStr(finishToken());

    unsigned keyid = lookupKeyword(str.c_str());
    if (keyid) {
      return Token(keyid, str, sloc);
    }
    return Token(TK_Identifier, str, sloc);
  }

  // generic operators
  if (isOperatorChar(c)) {
    readOperator(c);
    StringRef str = copyStr(finishToken());

    unsigned keyid = lookupKeyword(str.c_str());
    if (keyid) {
      return Token(keyid, str, sloc);
    }
    return Token(TK_Operator, str, sloc);
  }

  // numbers
  if (isDigit(c)) {
    readInteger(c);
    StringRef str = copyStr(finishToken());
    return Token(TK_LitInteger, str, sloc);
  }

  // characters
  if (c == '\'') {
    if (!readCharacter())
      return Token(TK_Error);

    StringRef str = copyStr(finishToken());
    return Token(TK_LitCharacter, str, sloc);
  }

  // strings
  if (c == '\"') {
    if (!readString())
      return Token(TK_Error);

    StringRef str = copyStr(finishToken());
    return Token(TK_LitString, str, sloc);
  }

  // if we're out of buffer, put in an EOF token.
  if (c == 0 || stream_eof()) {
    return Token(TK_EOF, "", sloc);
  }

  // Can't get the next token -- signal an error and bail.
  signalLexicalError();
  return Token(TK_Error, "", sloc);
}

} // end namespace parsing

} // end namespace ohmu
