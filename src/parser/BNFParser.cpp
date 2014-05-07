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


void BNFParser::defineSyntax() {
  PNamedRule sequence(this, "sequence");
  PNamedRule option(this, "option");
  PNamedRule astNode(this, "astNode");

  // astNodeList ::= 
  //   { [] }
  //   |*(es)  e=astNode { (append es e) };
  PNamedRule astNodeList(this, "astNodeList");
  astNodeList %=
    PLet("es", PReturn(new ast::EmptyList()))
    ^= (PLet("e", astNode.ref()) >>= PReturn("append", "es", "e"));

  // astNode ::= 
  //    s=%TK_LitString   { (tokenStr s)  }
  //    id=%TK_Identifier { (variable id) }
  //    "(" f=%TK_Identifier args=astNodeList ")" { (construct f args) };
  astNode %=
       (PLet("s", PToken(TK_LitString))   >>= PReturn("tokenStr", "s"))
    |= (PLet("id", PToken(TK_Identifier)) >>= PReturn("variable", "id"))
    |= (PKeyword("(") >>=
        PLet("f", PToken(TK_Identifier)) >>=
        PLet("args", astNodeList.ref()) >>=
        PKeyword(")") >>=
        PReturn("construct", "f", "args"));

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
        astNode.ref() >>=
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
  //   e1=option ( "|*" "(" id=%TK_Identifier ")" { (recurseLeft e1 e2 ) }
  //             | {e1}
  //             );
  PNamedRule recurseLeft(this, "recurseLeft");
  recurseLeft %=
    PLet("e1", option.ref()) >>=
      (  (PKeyword("|*") >>= 
          PKeyword("(") >>=
          PLet("id", PToken(TK_Identifier)) >>= 
          PKeyword(")") >>=
          PLet("e", sequence.ref()) >>=
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
