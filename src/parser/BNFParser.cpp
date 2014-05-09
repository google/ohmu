//===- BNFParser.cpp -------------------------------------------*- C++ --*-===//
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

#include "BNFParser.h"

namespace ohmu {
namespace parsing {


const char* BNFParser::getOpcodeName(BNF_Opcode op) {
  switch (op) {
    case BNF_None:             return "none";
    case BNF_Token:            return "token";
    case BNF_Keyword:          return "keyword";
    case BNF_Sequence:         return "sequence";
    case BNF_Option:           return "option";
    case BNF_RecurseLeft:      return "recurseLeft";
    case BNF_Reference:        return "reference";
    case BNF_NamedDefinition:  return "namedDefinition";
    case BNF_Action:           return "action";

    case BNF_Variable:         return "variable";
    case BNF_TokenStr:         return "tokenStr";
    case BNF_Construct:        return "construct";
    case BNF_EmptyList:        return "emptyList";
    case BNF_Append:           return "append";
  }
  return nullptr;
}


void BNFParser::initMap() {
  for (unsigned op = BNF_None; op <= BNF_Append; ++op) {
    opcodeNameMap_.emplace(getOpcodeName(static_cast<BNF_Opcode>(op)), op);
  }
}


unsigned BNFParser::lookupOpcode(const std::string &s) {
  auto it = opcodeNameMap_.find(s);
  if (it != opcodeNameMap_.end())
    return it->second;
  return BNF_None;
}


ParseResult BNFParser::makeExpr(unsigned op, unsigned arity, ParseResult *prs) {
  auto tok = [=](unsigned i) -> Token* {
    if (!prs[i].isToken() || i >= arity)
      return nullptr;
    return prs[i].getToken();
  };
  auto tokList = [=](unsigned i) -> std::vector< Token* >* {
    if (!prs[i].isToken() || i >= arity)
      return nullptr;
    return prs[i].getTokenList();
  };
  auto pr = [=](unsigned i) -> ParseRule* {
    if (!prs[i].isSingle(BPR_ParseRule) || i >= arity)
      return nullptr;
    return prs[i].getNode<ParseRule>(BPR_ParseRule);
  };
  auto astn = [=](unsigned i) -> ast::ASTNode* {
    if (!prs[i].isSingle(BPR_ASTNode) || i >= arity)
      return nullptr;
    return prs[i].getNode<ast::ASTNode>(BPR_ASTNode);
  };
  auto astList = [=](unsigned i) -> std::vector< ast::ASTNode* >* {
    if (!prs[i].isList(BPR_ASTNode) || i >= arity)
      return nullptr;
    return prs[i].getList<ast::ASTNode>(BPR_ASTNode);
  };

  switch (op) {
    case BNF_None: {
      return ParseResult(BPR_ParseRule, new ParseNone());
    }
    case BNF_Token: {
      Token *t = tok(0);
      unsigned tid = lookupTokenID(t->cppString());
      delete t;
      return ParseResult(BPR_ParseRule, new ParseToken(tid));
    }
    case BNF_Keyword: {
      Token *t = tok(0);
      auto r = new ParseKeyword(t->cppString());
      delete t;
      return ParseResult(BPR_ParseRule, r);
    }
    case BNF_Sequence: {
      assert(arity == 2 || arity == 3);
      if (arity == 2) {
        auto r = new ParseSequence("", pr(0), pr(1));
        return ParseResult(BPR_ParseRule, r);
      }
      if (arity == 3) {
        Token *t = tok(0);
        auto r = new ParseSequence(t->cppString(), pr(1), pr(2));
        delete t;
        return ParseResult(BPR_ParseRule, r);
      }
      return ParseResult();
    }
    case BNF_Option: {
      assert(arity == 2);
      auto r = new ParseOption(pr(0), pr(1));
      return ParseResult(BPR_ParseRule, r);
    }
    case BNF_RecurseLeft: {
      assert(arity == 3);
      Token *t = tok(0);
      auto r = new ParseRecurseLeft(t->cppString(), pr(1), pr(2));
      delete t;
      return ParseResult(BPR_ParseRule, r);
    }
    case BNF_Reference: {
      assert(arity == 2);
      Token *t = tok(0);
      auto r = new ParseReference(t->cppString());
      delete t;
      // get argument list
      auto *v = tokList(1);
      if (v) {
        for (Token *at : *v) {
          r->addArgument(at->cppString());
          delete at;
        }
        delete v;
      }
      return ParseResult(BPR_ParseRule, r);
    }
    case BNF_NamedDefinition: {
      assert(arity == 3);
      Token *t = tok(0);
      auto r = new ParseNamedDefinition(t->cppString(), pr(2));
      delete t;
      // get argument list
      auto *v = tokList(1);
      if (v) {
        for (Token *at : *v) {
          r->addArgument(at->cppString());
          delete at;
        }
        delete v;
      }
      return ParseResult(BPR_ParseRule, r);
    }
    case BNF_Action: {
      auto r = new ParseAction(astn(0));
      return ParseResult(BPR_ParseRule, r);
    }

    case BNF_Variable: {
      Token *t = tok(0);
      auto e = new ast::Variable(t->cppString());
      delete t;
      return ParseResult(BPR_ASTNode, e);
    }
    case BNF_TokenStr: {
      Token *t = tok(0);
      auto e = new ast::TokenStr(t->cppString());
      delete t;
      return ParseResult(BPR_ASTNode, e);
    }
    case BNF_Construct: {
      Token *t = tok(0);
      auto v = astList(1);
      ast::Construct* c = nullptr;

      if (!v) {
        c = new ast::ConstructN<0>(t->cppString());
      }
      else {
        switch (v->size()) {
          case 1:
            c = new ast::ConstructN<1>(t->cppString(), (*v)[0]);
            break;
          case 2:
            c = new ast::ConstructN<2>(t->cppString(), (*v)[0], (*v)[1]);
            break;
          case 3:
            c = new ast::ConstructN<3>(t->cppString(), (*v)[0], (*v)[1],
                                       (*v)[2]);
            break;
          case 4:
            c = new ast::ConstructN<4>(t->cppString(), (*v)[0], (*v)[1],
                                       (*v)[2], (*v)[3]);
            break;
          case 5:
            c = new ast::ConstructN<5>(t->cppString(), (*v)[0], (*v)[1],
                                       (*v)[2], (*v)[3], (*v)[4]);
            break;
        }
      }
      delete t;
      delete v;
      return ParseResult(BPR_ASTNode, c);
    }
    case BNF_EmptyList: {
      return ParseResult(BPR_ASTNode, new ast::EmptyList());
    }
    case BNF_Append: {
      auto e = new ast::Append(astn(0), astn(1));
      return ParseResult(BPR_ASTNode, e);
    }
    default:
      return ParseResult();
  }
}


void BNFParser::defineSyntax() {
  PNamedRule sequence(this, "sequence");
  PNamedRule option(this, "option");
  PNamedRule astNodeList(this, "astNodeList");

  // astNode ::=
  //    s=%TK_LitString   { (tokenStr s)  }
  //    id=%TK_Identifier { (variable id) }
  //    "(" f=%TK_Identifier args=astNodeList ")" { (construct f args) };
  PNamedRule astNode(this, "astNode");
  astNode %=
       (PLet("s", PToken(TK_LitString))   >>= PReturn("tokenStr", "s"))
    |= (PLet("id", PToken(TK_Identifier)) >>= PReturn("variable", "id"))
    |= (PKeyword("(") >>=
        PLet("f", PToken(TK_Identifier)) >>=
        PLet("args", astNodeList.ref()) >>=
        PKeyword(")") >>=
        PReturn("construct", "f", "args"));

  // astNodeList ::=
  //   { [] }
  //   |*(es)  e=astNode { (append es e) };
  astNodeList %=
    PLet("es", PReturn(new ast::EmptyList()))
    ^= (PLet("e", astNode.ref()) >>= PReturn("append", "es", "e"));

  // simple ::=
  //     s=%TK_LitString      { (keyword s) }
  //   | "%" s=%TK_Identifier { (token s)   }
  //   | "(" e=option ")"     { e }
  //   | "{" e=astNode "}"    { (action e)  };
  PNamedRule simple(this, "simple");
  simple %=
       (PLet("s", PToken(TK_LitString)) >>= PReturn("keyword", "s"))
    |= (PKeyword("%") >>=
        PLet("s", PToken(TK_Identifier)) >>=
        PReturn("token", "s"))
    |= (PKeyword("(") >>=
        PLet("e", option.ref()) >>=
        PKeyword(")") >>=
        PReturnVar("e"))
    |= (PKeyword("{") >>=
        PLet("e", astNode.ref()) >>=
        PKeyword("}") >>=
        PReturn("action", "e"));


  // arguments ::=
  //   id=%TK_Identifier { (append [] id) }
  //   |*(as) "," id=%TK_Identifier { (append as id) };
  PNamedRule arguments(this, "arguments");
  arguments %=
    PLet("as",
       (PLet("id", PToken(TK_Identifier)) >>=
        PReturn("append", nullptr, "id")))
    ^= (PKeyword(",") >>= PLet("id", PToken(TK_Identifier)) >>=
        PReturn("append", "as", "id"));

  // // Parse arguments, if any, and construct a reference from id
  // reference[id] ::=
  //     "[" as=arguments "]" { (reference id as) }
  //   | { (reference id []) };
  PNamedRule reference(this, "reference");
  reference["id"] %=
       (PKeyword("[") >>=
        PLet("as", arguments.ref()) >>=
        PKeyword("]") >>=
        PReturn("reference", "id", "as"))
    |= PReturn("reference", "id", nullptr);

  // simpleCall ::=
  //     simple
  //   | id=%TK_Identifier reference[id]
  PNamedRule simpleCall(this, "simpleCall");
  simpleCall %=
       simple.ref()
    |= (PLet("id", PToken(TK_Identifier)) >>= reference.ref("id"));

  // // Continue the sequence if possible, otherwise stop and return e.
  // maybeSequence[e] ::=
  //     sq=sequence { (sequence e sq) }
  //   | { e };
  PNamedRule maybeSequence(this, "maybeSequence");
  maybeSequence["e"] %=
        (PLet("sq", sequence.ref()) >>= PReturn("sequence", "e", "sq"))
     |= PReturnVar("e");

  // sequence ::=
  //     e=simple  maybeSequence(e)
  //   | id=%TK_Identifier ( "=" e=simpleCall sq=sequence { (sequence id e sq) }
  //                       | e=reference[id]  maybeSequence(e)
  //                       );
  sequence %=
       (PLet("e", simple.ref()) >>= maybeSequence.ref("e"))
    |= (PLet("id", PToken(TK_Identifier)) >>=
          (  (PKeyword("=") >>=
              PLet("e", simpleCall.ref()) >>=
              PLet("sq", sequence.ref()) >>=
              PReturn("sequence", "id", "e", "sq"))
          |= (PLet("e", reference.ref("id")) >>= maybeSequence.ref("e"))
          )
       );

  // option ::=
  //   e1=sequence ( "|" e2=option { (option e1 e2) }
  //               | {e1}
  //               );
  option %=
    PLet("e1", sequence.ref()) >>=
      (  (PKeyword("|") >>=
          PLet("e2", option.ref()) >>=
          PReturn("option", "e1", "e2"))
      |= PReturnVar("e1")
      );

  // recurseLeft ::=
  //   e1=option ( "|*" "(" id=%TK_Identifier ")" e2=sequence
  //               { (recurseLeft e1 e2 ) }
  //             | {e1}
  //             );
  PNamedRule recurseLeft(this, "recurseLeft");
  recurseLeft %=
    PLet("e1", option.ref()) >>=
      (  (PKeyword("|*") >>=
          PKeyword("(") >>=
          PLet("id", PToken(TK_Identifier)) >>=
          PKeyword(")") >>=
          PLet("e2", sequence.ref()) >>=
          PReturn("recurseLeft", "id", "e1", "e2"))
      |= PReturnVar("e1")
      );

  // maybeArguments ::=
  //     "[" as=arguments "]" {as}
  //   | { [] };
  PNamedRule maybeArguments(this, "maybeArguments");
  maybeArguments %=
       (PKeyword("[") >>=
        PLet("as", arguments.ref()) >>=
        PKeyword("]") >>=
        PReturnVar("as"))
    |= PReturn(new ast::EmptyList());

  // definition ::=
  //      id=%TK_Identifier as=maybeArguments "::=" e=recurseLeft ";"
  //        { (definition id as e) };
  PNamedRule definition(this, "definition");
  definition %=
    PLet("id", PToken(TK_Identifier)) >>=
    PLet("as", maybeArguments.ref()) >>=
    PKeyword("::=") >>=
    PLet("e", recurseLeft.ref()) >>=
    PKeyword(";") >>=
    PReturn("definition", "id", "as", "e");

  // definitionList ::=
  //   { [] }
  //   |*(ds) d=definition { (append ds d) }
  PNamedRule definitionList(this, "definitionList");
  definitionList %=
    PLet("ds", PReturn(new ast::EmptyList()))
    ^= ( PLet("d", definition.ref()) >>= PReturn("append", "ds", "d") );
};


}  // end namespace parser
}  // end namespace ohmu
