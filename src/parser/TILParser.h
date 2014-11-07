//===- TILParser.h ---------------------------------------------*- C++ --*-===//
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
// TILParser is a concrete parser, which constructs TIL expressions.
// The TIL grammar is read from an external grammar file.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_PARSER_H
#define OHMU_TIL_PARSER_H

#include "parser/Parser.h"
#include "parser/BNFParser.h"
#include "til/TIL.h"


namespace ohmu {
namespace parsing {

using namespace ohmu::til;


class TILParser : public Parser {
public:

  // The set of opcodes that are allowed to appear in astNode constructors.
  // This mostly mirrors TIL_Opcode, but there are some differences, e
  // especially with regard to literals and variables.
  enum TIL_ConstructOp {
    TCOP_LitNull,
    TCOP_LitBool,
    TCOP_LitChar,
    TCOP_LitInteger,
    TCOP_LitFloat,
    TCOP_LitString,

    TCOP_Identifier,
    TCOP_Function,
    TCOP_SFunction,
    TCOP_Code,
    TCOP_Field,
    TCOP_Record,
    TCOP_Slot,
    TCOP_Array,

    TCOP_Apply,
    TCOP_SApply,
    TCOP_Project,
    TCOP_Call,

    TCOP_Alloc,
    TCOP_Load,
    TCOP_Store,
    TCOP_ArrayIndex,
    TCOP_ArrayAdd,

    TCOP_UnaryOp,
    TCOP_BinaryOp,
    TCOP_Cast,

    TCOP_Let,
    TCOP_Letrec,
    TCOP_If
  };

  static const unsigned short TCOP_MAX = TCOP_If;

  // All parse rules return SExprs.
  static const unsigned short TILP_SExpr = ParseResult::PRS_UserDefined;


  TILParser(Lexer *lexer) : Parser(lexer) {
    initMap();
  }
  ~TILParser() { }

  MemRegionRef arena() { return arena_; }

  void setArenas(MemRegionRef strArena, MemRegionRef parseArena) {
    arena_ = parseArena;
    stringArena_ = strArena;
  }

  const char* getOpcodeName(TIL_ConstructOp op);

  void initMap();

  StringRef copyStr  (StringRef s);
  bool      toBool   (StringRef s);
  char      toChar   (StringRef s);
  int       toInteger(StringRef s);
  double    toDouble (StringRef s);
  StringRef toString (StringRef s);

  unsigned lookupOpcode(const std::string &s) override;

  TIL_UnaryOpcode  lookupUnaryOpcode(StringRef s);
  TIL_BinaryOpcode lookupBinaryOpcode(StringRef s);
  TIL_CastOpcode   lookupCastOpcode(StringRef s);

  ParseResult makeExpr(unsigned op, unsigned arity, ParseResult *prs) override;

private:
   MemRegionRef arena_;
   MemRegionRef stringArena_;

   std::unordered_map<std::string, unsigned> opcodeMap_;
   std::unordered_map<std::string, unsigned> unaryOpcodeMap_;
   std::unordered_map<std::string, unsigned> binaryOpcodeMap_;
   std::unordered_map<std::string, unsigned> castOpcodeMap_;
};


}  // end namespace parsing
}  // end namespace ohmu

#endif  // OHMU_TIL_PARSER_H
