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

#include "parser/TILParser.h"

#include <cstdlib>


namespace ohmu {
namespace parsing {

using namespace ohmu::til;


const char* TILParser::getOpcodeName(TIL_ConstructOp op) {
  switch (op) {
    case TCOP_LitNull:    return "litNull";
    case TCOP_LitBool:    return "litBool";
    case TCOP_LitChar:    return "litChar";
    case TCOP_LitInteger: return "litInteger";
    case TCOP_LitFloat:   return "litFloat";
    case TCOP_LitString:  return "litString";

    case TCOP_Identifier: return "identifier";
    case TCOP_Function:   return "function";
    case TCOP_SFunction:  return "sfunction";
    case TCOP_Code:       return "code";
    case TCOP_Field:      return "field";
    case TCOP_Record:     return "record";
    case TCOP_Slot:       return "slot";
    case TCOP_Array:      return "array";

    case TCOP_Apply:      return "apply";
    case TCOP_SApply:     return "sapply";
    case TCOP_Project:    return "project";
    case TCOP_Call:       return "call";

    case TCOP_Alloc:      return "alloc";
    case TCOP_Load:       return "load";
    case TCOP_Store:      return "store";
    case TCOP_ArrayIndex: return "arrayIndex";
    case TCOP_ArrayAdd:   return "arrayAdd";

    case TCOP_UnaryOp:    return "unary";
    case TCOP_BinaryOp:   return "binary";
    case TCOP_Cast:       return "cast";

    case TCOP_Let:        return "let";
    case TCOP_If:         return "if";
    default:              return nullptr;
  }
}


void TILParser::initMap() {
  for (unsigned op = TCOP_LitNull; op <= TCOP_MAX; ++op) {
    opcodeMap_.emplace(getOpcodeName(static_cast<TIL_ConstructOp>(op)), op);
  }
  for (unsigned op = UOP_Min; op <= UOP_Max; ++op) {
    unaryOpcodeMap_.emplace(
      getUnaryOpcodeString(static_cast<TIL_UnaryOpcode>(op)).str(), op);
  }
  for (unsigned op = BOP_Min; op <= BOP_Max; ++op) {
    binaryOpcodeMap_.emplace(
      getBinaryOpcodeString(static_cast<TIL_BinaryOpcode>(op)).str(), op);
  }
}


template<class EnumT>
inline EnumT lookup(std::unordered_map<std::string, unsigned>& map,
                    const std::string& s, EnumT def) {
  auto it = map.find(s);
  if (it != map.end())
    return static_cast<EnumT>(it->second);
  // FIXME: should be an error message here!
  return def;
}

unsigned TILParser::lookupOpcode(const std::string &s) {
  return lookup<unsigned>(opcodeMap_, s, ast::Construct::InvalidOpcode);
}

TIL_UnaryOpcode TILParser::lookupUnaryOpcode(StringRef s) {
  return lookup(unaryOpcodeMap_, s.str(), UOP_LogicNot);
}

TIL_BinaryOpcode TILParser::lookupBinaryOpcode(StringRef s) {
  return lookup(binaryOpcodeMap_, s.str(), BOP_Add);
}

TIL_CastOpcode TILParser::lookupCastOpcode(StringRef s) {
  return lookup(castOpcodeMap_, s.str(), CAST_none);
}


inline StringRef TILParser::copyStr(StringRef s) {
  // Put all strings in the string arena, which must survive
  // for the duration of the compile.
  char* temp = reinterpret_cast<char*>(stringArena_.allocate(s.size()+1));
  return copyStringRef(temp, s);
}

bool TILParser::toBool(StringRef s) {
  if (s == "true") return true;
  else if (s == "false") return false;
  assert(false);
  return false;
}

char TILParser::toChar(StringRef s) {
  return s.c_str()[0];
}

int TILParser::toInteger(StringRef s) {
  char* end = nullptr;
  long long val = strtol(s.c_str(), &end, 0);
  // FIXME: some proper error handling here?
  assert(end == s.c_str() + s.size() && "Could not parse string.");
  return static_cast<int>(val);
}

double TILParser::toDouble(StringRef s) {
  char* end = nullptr;
  double val = strtod(s.c_str(), &end);
  // FIXME: some proper error handling here?
  assert(end == s.c_str() + s.size() && "Could not parse string.");
  return val;
}

StringRef TILParser::toString(StringRef s) {
  return copyStr(s);  // Lexer has already stripped the ""
}


ParseResult TILParser::makeExpr(unsigned op, unsigned arity, ParseResult *prs) {
  auto tok = [=](unsigned i) -> Token* {
    if (!prs[i].isToken() || i >= arity)
      return nullptr;
    return prs[i].getToken();
  };
  /*
  auto tokList = [=](unsigned i) -> std::vector< Token* >* {
    if (!prs[i].isTokenList() || i >= arity)
      return nullptr;
    return prs[i].getTokenList();
  };
  */
  auto sexpr = [=](unsigned i) -> SExpr* {
    if (!prs[i].isSingle(TILP_SExpr) || i >= arity)
      return nullptr;
    return prs[i].getNode<SExpr>(TILP_SExpr);
  };
  auto sexprList = [=](unsigned i) -> std::vector<SExpr*>* {
    if (!prs[i].isList(TILP_SExpr) || i >= arity)
      return nullptr;
    return prs[i].getList<SExpr>(TILP_SExpr);
  };

  switch (op) {
    case TCOP_LitNull: {
      assert(arity == 0);
      return ParseResult();
    }
    case TCOP_LitBool: {
      assert(arity == 1);
      Token *t = tok(0);
      auto* e = new (arena_) LiteralT<bool>(toBool(t->string()));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_LitChar: {
      assert(arity == 1);
      Token *t = tok(0);
      auto* e = new (arena_) LiteralT<uint8_t>(toChar(t->string()));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_LitInteger: {
      assert(arity == 1);
      Token *t = tok(0);
      auto* e = new (arena_) LiteralT<int>(toInteger(t->string()));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_LitFloat: {
      assert(arity == 1);
      Token *t = tok(0);
      auto* e = new (arena_) LiteralT<double>(toDouble(t->string()));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_LitString: {
      assert(arity == 1);
      Token* t = tok(0);
      auto* e = new (arena_) LiteralT<StringRef>(toString(t->string()));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }

    case TCOP_Identifier: {
      assert(arity == 1);
      Token* t = tok(0);
      auto* e = new (arena_) Identifier(copyStr(t->string()));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_Function: {
      assert(arity == 3);
      Token* t = tok(0);
      auto* v = new (arena_) VarDecl(VarDecl::VK_Fun,
                                     copyStr(t->string()), sexpr(1));
      auto* e = new (arena_) Function(v, sexpr(2));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_SFunction: {
      assert(arity == 2);
      Token* t = tok(0);
      auto* v = new (arena_) VarDecl(VarDecl::VK_SFun,
                                     copyStr(t->string()), nullptr);
      auto* e = new (arena_) Function(v, sexpr(1));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_Code: {
      assert(arity == 2);
      auto* e = new (arena_) Code(sexpr(0), sexpr(1));
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_Field: {
      assert(arity == 2);
      auto* e = new (arena_) Field(sexpr(0), sexpr(1));
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_Record: {
      assert(arity == 1);
      auto* es = sexprList(0);
      auto* r  = new (arena_) Record(arena_, es->size());
      for (SExpr* e : *es) {
        Slot* s = dyn_cast<Slot>(e);
        if (s)
          r->slots().emplace_back(arena_, s);
      }
      delete es;
      return ParseResult(TILP_SExpr, r);
    }
    case TCOP_Slot: {
      assert(arity == 2);
      Token* t = tok(0);
      SExpr* d = sexpr(1);
      auto* s = new (arena_) Slot(copyStr(t->string()), d);
      delete t;
      return ParseResult(TILP_SExpr, s);
    }
    case TCOP_Array: {
      assert(arity == 2);
      return ParseResult();
    }

    case TCOP_Apply: {
      assert(arity == 2);
      auto* e = new (arena_) Apply(sexpr(0), sexpr(1));
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_SApply: {
      assert(arity == 1 || arity == 2);
      SExpr* e = nullptr;
      if (arity == 1)
        e = new (arena_) Apply(sexpr(0), nullptr,  Apply::FAK_SApply);
      else if (arity == 2)
        e = new (arena_) Apply(sexpr(0), sexpr(1), Apply::FAK_SApply);
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_Project: {
      assert(arity == 2);
      Token* t = tok(1);
      auto* e = new (arena_) Project(sexpr(0), copyStr(t->string()));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_Call: {
      assert(arity == 1);
      auto* e = new (arena_) Call(sexpr(0));
      return ParseResult(TILP_SExpr, e);
    }

    case TCOP_Alloc: {
      assert(arity == 1);
      auto* e = new (arena_) Alloc(sexpr(0), Alloc::AK_Local);
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_Load: {
      assert(arity == 1);
      auto* e = new (arena_) Load(sexpr(0));
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_Store: {
      assert(arity == 2);
      auto* e = new (arena_) Store(sexpr(0), sexpr(1));
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_ArrayIndex: {
      assert(arity == 2);
      auto* e = new (arena_) ArrayIndex(sexpr(0), sexpr(1));
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_ArrayAdd: {
      assert(arity == 2);
      auto* e = new (arena_) ArrayAdd(sexpr(0), sexpr(1));
      return ParseResult(TILP_SExpr, e);
    }

    case TCOP_UnaryOp: {
      assert(arity == 2);
      Token* t = tok(0);
      TIL_UnaryOpcode op = lookupUnaryOpcode(t->string());
      delete t;
      auto* e = new (arena_) UnaryOp(op, sexpr(1));
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_BinaryOp: {
      assert(arity == 3);
      Token* t = tok(0);
      TIL_BinaryOpcode op = lookupBinaryOpcode(t->string());
      delete t;
      auto* e = new (arena_) BinaryOp(op, sexpr(1), sexpr(2));
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_Cast: {
      assert(arity == 2);
      Token* t = tok(0);
      TIL_CastOpcode op = lookupCastOpcode(t->string());
      delete t;
      auto* e = new (arena_) Cast(op, sexpr(1));
      return ParseResult(TILP_SExpr, e);
    }

    case TCOP_Let: {
      assert(arity == 3);
      Token* t = tok(0);
      auto* v = new (arena_) VarDecl(VarDecl::VK_Let,
                                     copyStr(t->string()), sexpr(1));
      auto* e = new (arena_) Let(v, sexpr(2));
      delete t;
      return ParseResult(TILP_SExpr, e);
    }
    case TCOP_If: {
      assert(arity == 3);
      auto *e = new (arena_) IfThenElse(sexpr(0), sexpr(1), sexpr(2));
      return ParseResult(TILP_SExpr, e);
    }

    default:
      return ParseResult();
  }
}


}  // end namespace parsing
}  // end namespace ohmu



