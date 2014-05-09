//===- Parser.h ------------------------------------------------*- C++ --*-===//
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
// This file defines a library for constructing LL(k) parsers.  The library is
// based on the idea of "parser combinators", in which larger parsers are
// constructed from smaller ones.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_PARSER_H
#define OHMU_PARSER_H

#include "ASTNode.h"
#include "Lexer.h"

#include <ostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ohmu {

namespace parsing {

enum ParseRuleKind {
  PR_None,
  PR_Token,
  PR_Keyword,
  PR_Sequence,
  PR_Option,
  PR_RecurseLeft,
  PR_Reference,
  PR_Action,
  PR_NamedDefinition,
};


class Parser;
class ParseNamedDefinition;


// Base class for parse rules
class ParseRule {
public:
  ParseRule(ParseRuleKind k) : kind_(k) { }
  virtual ~ParseRule() { }

  // Performs parser initialization associated with this rule.
  // tail is true for combinators in a tail-call position.
  virtual bool init(Parser& parser) = 0;

  // Return true if the rule accepts tok as the initial token.
  virtual bool accepts(const Token& tok) = 0;

  // Parse input using the current rule.  Returns the the next rule
  // that should be used to parse input.
  virtual ParseRule* parse(Parser& parser) = 0;

  virtual void prettyPrint(Parser& parser, std::ostream& out) = 0;

  inline ParseRuleKind kind() const { return kind_; }

private:
  ParseRuleKind kind_;
};


// Matches the empty input.
// This can be used in an option, but it should only appear as the last option.
class ParseNone : public ParseRule {
public:
  static bool classof(const ParseRule *pr) { return pr->kind() == PR_None; }

  ParseNone() : ParseRule(PR_None) { }
  ~ParseNone() { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override ;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;
};


// Matches a single token of input, with a type that is predefined
// by the lexer.  Does not alter parsing state.
class ParseToken : public ParseRule {
public:
  static bool classof(const ParseRule *pr) { return pr->kind() == PR_Token; }

  ParseToken(unsigned tid, bool skip=false)
    : ParseRule(PR_Token), tokenID_(tid), skip_(skip)
  { }
  ~ParseToken() { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

protected:
  ParseToken(ParseRuleKind k, unsigned tid, bool skip)
    : ParseRule(k), tokenID_(tid), skip_(skip)
  { }

  unsigned tokenID_;
  bool skip_;
};


// Matches a single keyword.  The keyword is registered with the lexer
// as a new token at the start of parsing.
class ParseKeyword : public ParseToken {
public:
  static bool classof(const ParseRule *pr) { return pr->kind() == PR_Keyword; }

  ParseKeyword(std::string s)
    : ParseToken(PR_Keyword, 0, true), keywordStr_(std::move(s))
  { }
  ~ParseKeyword() { }

  bool init(Parser& parser) override;
  void prettyPrint(Parser& parser, std::ostream& out) override;

private:
  const std::string keywordStr_;
};


// Matches a sequence of input.
class ParseSequence : public ParseRule {
public:
  static bool classof(const ParseRule *pr) { return pr->kind() == PR_Sequence; }

  ParseSequence(std::string letName,
                ParseRule *first, ParseRule *second)
    : ParseRule(PR_Sequence), letName_(std::move(letName)),
      first_(first), second_(second)
  { }
  ~ParseSequence() {
    if (first_)  delete first_;
    if (second_) delete second_;
  }

  // Returns true if the head of this sequence has a name.
  inline bool hasLetName() { return letName_.length() > 0; }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

 private:
  std::string letName_;
  ParseRule*  first_;
  ParseRule*  second_;
};


// Distinguishes between two options.
class ParseOption : public ParseRule {
public:
  static bool classof(const ParseRule *pr) { return pr->kind() == PR_Option; }

  ParseOption(ParseRule *left, ParseRule *right)
    : ParseRule(PR_Option), left_(left), right_(right)
  { }
  ~ParseOption() {
    if (left_)  delete left_;
    if (right_) delete right_;
  }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

 private:
  ParseRule* left_;
  ParseRule* right_;
};


// Builds a left-recursive parse rule
class ParseRecurseLeft : public ParseRule {
public:
  static bool classof(const ParseRule *pr) {
    return pr->kind() == PR_RecurseLeft;
  }

  ParseRecurseLeft(std::string letName, ParseRule *base, ParseRule *rest)
    : ParseRule(PR_RecurseLeft), letName_(std::move(letName)),
      base_(base), rest_(rest)
  { }
  ~ParseRecurseLeft() {
    if (base_) delete base_;
    if (rest_) delete rest_;
  }

  inline bool hasLetName() { return letName_.length() > 0; }

  virtual bool       init(Parser& parser);
  virtual bool       accepts(const Token& tok);
  virtual ParseRule* parse(Parser& parser);
  virtual void       prettyPrint(Parser& parser, std::ostream& out);

private:
  std::string letName_;
  ParseRule*  base_;
  ParseRule*  rest_;
};


// A top-level named definition.
// Named definitions allow mutually recursive rules to be defined.
class ParseNamedDefinition : public ParseRule {
public:
  static bool classof(const ParseRule *pr) {
    return pr->kind() == PR_NamedDefinition;
  }

  ParseNamedDefinition(std::string name, ParseRule* r = nullptr)
    : ParseRule(PR_NamedDefinition), name_(std::move(name)), rule_(r)
  { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

  const std::string& name() const { return name_; }
  unsigned numArguments()   const { return argNames_.size(); }

  void addArgument(std::string s) { argNames_.push_back(std::move(s)); }

  void setDefinition(ParseRule* rule) { rule_ = rule; }

private:
  std::string              name_;
  std::vector<std::string> argNames_;
  ParseRule*               rule_;
};


// Refers to another named top-level parse rule.
// Can "call" the named rule by passing arguments.
class ParseReference : public ParseRule {
public:
  static bool classof(const ParseRule *pr) {
    return pr->kind() == PR_Reference;
  }

  ParseReference(ParseNamedDefinition* def)
    : ParseRule(PR_Reference), name_(def->name()), definition_(def),
      frameSize_(0), drop_(0)
  { }
  ParseReference(std::string name)
    : ParseRule(PR_Reference), name_(std::move(name)), definition_(nullptr),
      frameSize_(0), drop_(0)
  { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

  inline void addArgument(std::string arg) {
    argNames_.emplace_back(std::move(arg));
  }

 private:
  std::string              name_;
  ParseNamedDefinition*    definition_;
  std::vector<std::string> argNames_;   // argument names
  std::vector<unsigned>    arguments_;  // stack indices of arguments
  unsigned                 frameSize_;  // size of the stack frame
  unsigned                 drop_;       // num items to drop from the stack
};


// Constructs an expression in the target language.
// The ASTNode is interpreted to create the expression.
// Variables in the ASTNode refer to named results on the parser stack.
class ParseAction : public ParseRule {
public:
  static bool classof(const ParseRule *pr) { return pr->kind() == PR_Action; }

  ParseAction(ast::ASTNode *n)
    : ParseRule(PR_Action), node_(n), drop_(0)
  { }

  bool       init(Parser& parser) override;
  bool       accepts(const Token& tok) override;
  ParseRule* parse(Parser& parser) override;
  void       prettyPrint(Parser& parser, std::ostream& out) override;

private:
  ast::ASTNode* node_;    // ASTNode to interpret
  unsigned frameSize_;    // size of the stack frame
  unsigned drop_;         // num items to drop from the stack.
};


// The result of parsing a rule is a ParseResult, which can be either:
//   (1) A single token.
//   (2) A pointer to a user-defined AST Node
//   (3) A list of (1) or (2)
//
// ParseResults are move-only objects which hold unique pointers.
// Teading the result will relinquish ownership of the pointer.
// Failure to use a parse result is an error.
class ParseResult {
public:
  typedef unsigned char      KindType;
  typedef std::vector<void*> ListType;

  enum ResultKind {
    PRS_None = 0,
    PRS_TokenStr = 1,
    PRS_UserDefined = 2    // user-defined AST node type
  };

  ParseResult()
      : resultKind_(PRS_None), isList_(false), result_(nullptr)
  { }
  explicit ParseResult(Token* tok)
      : resultKind_(PRS_TokenStr), isList_(false), result_(tok)
  { }
  // Create a user defined AST Node; kinds specifies the kind of node.
  ParseResult(unsigned short kind, void* node)
      : resultKind_(kind), isList_(false), result_(node) {
    assert(kind >= PRS_UserDefined && "Invalid kind");
  }
  ~ParseResult() {
    // All ParseResults must be used.
    assert(resultKind_ == PRS_None && "Unused ParseResult.");
  }

  ParseResult(ParseResult &&r)
      : resultKind_(r.resultKind_), isList_(r.isList_), result_(r.result_) {
    r.release();
  }

  void operator=(ParseResult &&r) {
    resultKind_ = r.resultKind_;
    isList_ = r.isList_;
    result_ = r.result_;
    r.release();
  }

  KindType kind() const { return resultKind_; }

  bool isEmpty()            const { return resultKind_ == PRS_None; }
  bool isToken()            const { return kind() == PRS_TokenStr && !isList_; }
  bool isTokenList()        const { return kind() == PRS_TokenStr && isList_;  }
  bool isSingle(KindType k) const { return kind() == k && !isList_; }
  bool isList  (KindType k) const { return kind() == k && isList_; }
  bool isList  ()           const { return isList_; }

  // Return a token, and release ownership.
  Token* getToken() {
    assert(isToken());
    return getAs<Token>();
  }

  // Return a list of tokens, and release ownership.
  std::vector<Token*>* getTokenList() {
    assert(isTokenList());
    return getAs< std::vector<Token*> >();
  }

  // Return an AST node, and release ownership.
  template <class T>
  T* getNode(KindType k) {
    assert(isSingle(k));
    return getAs<T>();
  }

  // Return the node list, and release ownership.
  template <class T>
  std::vector<T*>* getList(KindType k) {
    assert(isList(k));
    return getAs< std::vector<T*> >();
  }

  // Append p to this list, and consume p.
  // If this is an empty result, create a new list.
  // Returns false on failure, if kind of p does not match.
  bool append(ParseResult&& p);

private:
  ParseResult(const ParseResult& r) = delete;
  void operator=(const ParseResult &f) = delete;

  inline void release() {       // release ownership of any data
    resultKind_ = PRS_None;
    isList_ = false;
    result_ = nullptr;
  }

  template<class T>
  inline T* getAs() {
    T* p = reinterpret_cast<T*>(result_);
    release();
    return p;
  }

  KindType resultKind_;
  bool     isList_;
  void*    result_;
};



// The result stack maintains a stack of ParseResults.
// It functions much like a program stack.
class ResultStack {
public:
  ResultStack() : stack_(0) { }

  unsigned size() const { return stack_.size(); }

  // move the argument at index i onto the top of the stack.
  void moveAndPush(unsigned i) {
    assert(i < stack_.size() && "Array index out of bounds.");
    std::cerr << "\n   movpush " << i << " -> " << stack_.size();
    stack_.emplace_back(std::move(stack_[i]));
    dump();
  }

  void push_back(const Token& tok) {
    std::cerr << "\n   push token " << stack_.size();
    stack_.emplace_back(ParseResult(new Token(tok)));
    dump();
  }

  void push_back(ParseResult &&r) {
    std::cerr << "\n   push result " << stack_.size();
    stack_.emplace_back(std::move(r));
    dump();
  }

  // Drop n items from the stack, but keep the nsave top-most items.
  void drop(unsigned n, unsigned nsave) {
    std::cerr << "\n   drop " << n << " " << nsave;
    if (n == 0)
      return;
    assert(stack_.size() >= n + nsave && "Stack too small");
    dump();
    stack_.erase(stack_.end()-nsave-n, stack_.end()-nsave);
    dump();
  }

  ParseResult getElem(unsigned i) {
    assert(i < stack_.size() && "Array index out of bounds.");
    std::cerr << "\n   read var " << i;
    ParseResult r = std::move(stack_[i]);
    dump();
    return std::move(r);
  }

  ParseResult getBack() { return std::move(stack_.back()); }

  void clear() {
    stack_.clear();
  }

  void dump();

private:
  std::vector<ParseResult> stack_;
};



// The abstract stack is used during initialization and validation.
// It mimics the behavior of ResultStack, but but holds the names of the
// results that will be produced during parsing.
// The abstract stack is used to validate the parser, and compute frame sizes
// and indices for named arguments.
class AbstractStack {
public:
  static const int InvalidIndex = 0xFFFF;
  typedef std::pair<unsigned, unsigned> FrameState;

  // Find the stack index for name s on the abstract stack.
  // Indices are computed with respect to the current frame.
  unsigned getIndex(const std::string &s) {
    for (unsigned i=0, n=stack_.size(); i<n; ++i) {
      if (stack_[i] && *stack_[i] == s) return i;
    }
    return InvalidIndex;  // failure.
  }

  // Return the size of the current stack frame.
  // (I.e. the size of the stack for the current named, top-level rule.)
  unsigned size() {  return stack_.size(); }

  // Return the size of the stack for the local block.
  // Tail calls unwind the stack by this amount during parsing.
  unsigned localSize() { return stack_.size() - blockStart_; }

  // Return the size of the stack for the current lexical scope.
  // Tail calls unwind the stack by this amount during validation.
  unsigned lexicalSize() {
    unsigned lst = (lexicalStart_ > blockStart_) ? lexicalStart_ : blockStart_;
    return size() - lst;
  }

  // Rewind the stack to the start of the current lexical scope.
  void rewind() {
    for (unsigned n = lexicalSize(); n > 0; --n)
      stack_.pop_back();
  }

  // Enter a new local block (i.e. new subrule).
  // This will also enter a new leixcal scope.  (See lexicalSize())
  unsigned enterLocalBlock() {
    unsigned bs = blockStart_;
    blockStart_ = stack_.size();
    return bs;
  }

  // Enter a new lexical scope.  Returns the old value to restore later.
  unsigned enterLexicalScope() {
    unsigned ls = lexicalStart_;
    lexicalStart_ = stack_.size();
    return ls;
  }

  // Exit the current local block.
  void exitLocalBlock(unsigned bs) {
    assert(bs <= stack_.size());
    blockStart_ = bs;
  }

  // Exit the current lexical scope, using the old value.
  void exitLexicalScope(unsigned ls) {
    assert(ls <= stack_.size() && ls >= blockStart_);
    lexicalStart_ = ls;
  }

  // get the ith value on the stack, starting from the current frame.
  std::string*& operator[](unsigned i) { return stack_[i]; }

  // push a new name onto the stack.
  void push_back(std::string *s) { stack_.push_back(s); }

  // pop a name off of the stack.
  void pop_back() {
    assert(lexicalSize() > 0);
    stack_.pop_back();
  }

  // clear the stack
  void clear() {
    blockStart_ = 0;
    lexicalStart_ = 0;
    stack_.clear();
  }

  void dump();

private:
  unsigned blockStart_ = 0;
  unsigned lexicalStart_ = 0;
  std::vector<std::string*> stack_;
};



class Parser {
public:
  // Create a new parser.
  Parser(Lexer* lexer) : lexer_(lexer) { }
  virtual ~Parser() { }

  // Override this to look up the opcode for a string.
  virtual unsigned lookupOpcode(const std::string &s) = 0;

  // Override this to construct an expression in the target language.
  virtual ParseResult makeExpr(unsigned op, unsigned arity, ParseResult *prs) = 0;

  // Initialize the parser, with rule as the starting point.
  bool init();

  // Parse rule and return the result.
  ParseResult parse(ParseNamedDefinition* start);

  // Add a new top-level named definition.
  void addDefinition(ParseNamedDefinition* def) {
    definitions_.push_back(def);
    const std::string& s = def->name();
    if (s.length() > 0)
      definitionDict_[s] = def;
  }

  // Find a top-level definition by name.
  ParseNamedDefinition* findDefinition(const std::string& s) {
    DefinitionDict::iterator it = definitionDict_.find(s);
    if (it == definitionDict_.end())
      return 0;
    else return it->second;
  }

  unsigned registerKeyword(const std::string& s) {
    return lexer_->registerKeyword(s);
  }

  const char* getTokenIDString(unsigned tid) {
    return lexer_->getTokenIDString(tid);
  }

  unsigned lookupTokenID(const std::string& s) {
    return lexer_->lookupTokenID(s);
  }

  void printSyntax(std::ostream& out);

  void setTrace(bool b)         { trace_ = b; }
  void setTraceValidate(bool b) { traceValidate_ = b; }

protected:
  typedef std::vector<ParseNamedDefinition*>           DefinitionVect;
  typedef std::map<std::string, ParseNamedDefinition*> DefinitionDict;

  friend class ParseRule;
  friend class ParseNone;
  friend class ParseToken;
  friend class ParseKeyword;
  friend class ParseSequence;
  friend class ParseOption;
  friend class ParseRecurseLeft;
  friend class ParseReference;
  friend class ParseNamedDefinition;
  friend class ParseAction;

  friend class ASTIndexVisitor;
  friend class ASTInterpretReducer;
  friend class TraceIndenter;
  friend class PrintIndenter;

  // Initialize rule p.  This is used internally to make recursive calls.
  inline bool initRule(ParseRule* p);

  // Parse rule p.  This is invoked internally to make recursive calls.
  inline void parseRule(ParseRule *p);

  // look at the next token
  const Token& look(unsigned i = 0) {
    return lexer_->look(i);
  }

  // consume next token from lexer, and discard it
  void skip() {
    lexer_->consume();
  }

  // consume next token from lexer, and push it onto the stack
  void consume() {
    resultStack_.push_back(look());
    lexer_->consume();
  }

  // output a parser validation error.
  std::ostream& validationError();

  // output a parser syntax error.
  std::ostream& parseError(const SourceLocation& sloc);

  void indent(std::ostream& out, unsigned n) {
    for (unsigned i=0; i<n; ++i) out << " ";
  }

private:
  Lexer*          lexer_ = nullptr;
  DefinitionVect  definitions_;
  DefinitionDict  definitionDict_;

  ResultStack     resultStack_;
  AbstractStack   abstractStack_;
  bool            parseError_ = false;

  // Used for debugging and pretty printing
  bool trace_ = false;
  bool traceValidate_ = false;
  unsigned traceIndent_ = 0;
  unsigned printIndent_ = 0;
};


} // end namespace parser

} // end namespace ohmu

#endif  // OHMU_PARSER_H
