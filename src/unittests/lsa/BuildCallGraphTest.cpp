#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"
#include "lsa/BuildCallGraph.h"


/// Helper function running actual test. Creates a virtual file with the
/// specified content and runs the call graph generation on it. It then checks
/// whether the generated call graph matches the provided expected mapping.
void TestCallGraph(
    const std::string &content,
    const std::map<std::string, std::vector<std::string>> &expected) {

  ohmu::lsa::DefaultCallGraphBuilder Builder;
  clang::ast_matchers::MatchFinder Finder;
  ohmu::lsa::CallGraphBuilderTool Tool;
  Tool.RegisterMatchers(Builder, &Finder);
  clang::FrontendAction *Action =
      clang::tooling::newFrontendActionFactory(&Finder)->create();
  ASSERT_TRUE(clang::tooling::runToolOnCode(Action, content));

  const auto& graph = Builder.GetGraph();
  EXPECT_EQ(expected.size(), graph.size());

  for (auto I = expected.begin(); I != expected.end(); ++I) {
    std::string Func = (*I).first;
    std::vector<std::string> ExpCalls = (*I).second;

    auto Element = graph.find(Func);
    EXPECT_NE(Element, graph.end()) << "Searching function-node " << Func
        << ".";

    if (Element != graph.end()) {
      ohmu::lsa::DefaultCallGraphBuilder::CallGraphNode *Node =
          (*Element).second.get();
      EXPECT_NE(Node, nullptr) << "Searching function-node " << Func << ".";

      if (Node != nullptr) {
        std::cout << Node->GetIR() << std::endl;
        const std::set<std::string> *Calls = Node->GetCalls();
        EXPECT_EQ(ExpCalls.size(), Calls->size()) << "Within function-node "
            << Func << ".";

        auto NotFound = Calls->end();
        for (auto C = ExpCalls.begin(); C != ExpCalls.end(); ++C) {
          EXPECT_NE(Calls->find(*C), NotFound) << "Within function-node "
              << Func << " did not find expected call " << (*C) << ".";
        }
      }
    }
  }
}

TEST(BuildCallGraph, BasicSingleFunction) {

  std::string data = "void f() { }";
  std::map<std::string, std::vector<std::string>> expected;
  expected["_Z1fv"] = std::vector<std::string>();

  TestCallGraph(data, expected);
}

TEST(BuildCallGraph, BasicFunctionCallGraph) {

  std::string data = "void i(); void j();                    "
                     "void f() { i(); j(); j(); }            "
                     "void g() { f(); }                      "
                     "void h() { f(); g(); }                 "
                     "void i() { g(); g(); h(); f(); g(); }  "
                     "void j() { }";

  const std::string f = "_Z1fv", g = "_Z1gv", h = "_Z1hv", i = "_Z1iv",
                    j = "_Z1jv";

  TestCallGraph(data,
                {{f, {i, j}}, {g, {f}}, {h, {f, g}}, {i, {f, g, h}}, {j, {}}});
}


TEST(BuildCallGraph, MemberFunction) {

  std::string data = "void g() { }                       "
                     "class B {                          "
                     "public:                            "
                     "  void m() { }                     "
                     "  void m(int x) { g(); }           "
                     "};                                 "
                     "void call() { B b; b.m(15); }      "
                     "void call(B *b) { b->m(); }        ";

  const std::string g = "_Z1gv", call = "_Z4callv", callB = "_Z4callP1B",
                    m = "_ZN1B1mEv", mInt = "_ZN1B1mEi", bCons = "_ZN1BC2Ev";

  TestCallGraph(data, {{g, {}},
                       {bCons, {}},
                       {m, {}},
                       {mInt, {g}},
                       {call, {mInt, bCons}},
                       {callB, {m}}});
}

TEST(BuildCallGraph, DestructorCall) {

  std::string data = "void g() { }                       "
                     "class B {                          "
                     "public:                            "
                     "  ~B() { g(); }                    "
                     "};                                 "
                     "void call() { B b;  }              ";

  const std::string g = "_Z1gv", call = "_Z4callv", bCons = "_ZN1BC2Ev",
                    bDest = "_ZN1BD2Ev";

  TestCallGraph(data,
                {{g, {}}, {bCons, {}}, {bDest, {g}}, {call, {bCons, bDest}}});
}

TEST(BuildCallGraph, TemplatedFunction) {

  std::string data = "void g() { }                            "
                     "template <class T>                      "
                     "void t(T t) { g(); }                    "
                     "void c() { t<bool>(false); t<int>(3); } "
                     "template <class T>                      "
                     "void cT(T t) { t.m(); }                 "
                     "class B {                               "
                     "public:                                 "
                     "  void m() { }                          "
                     "};                                      "
                     "void cB() { B b; cT(b); }               ";

  const std::string g = "_Z1gv", tBool = "_Z1tIbEvT_", tInt = "_Z1tIiEvT_",
                    c = "_Z1cv", cTB = "_Z2cTI1BEvT_", bCons = "_ZN1BC2Ev",
                    bCopy = "_ZN1BC2ERKS_", m = "_ZN1B1mEv", cB = "_Z2cBv";

  TestCallGraph(data, {{g, {}},
                       {tBool, {g}},
                       {tInt, {g}},
                       {c, {tBool, tInt}},
                       {cTB, {m}},
                       {bCons, {}},
                       {bCopy, {}},
                       {m, {}},
                       {cB, {bCopy, bCons, cTB}}});
}

TEST(BuildCallGraph, TemplatedSpecializeFunction) {

  std::string data = "void g() { }                            "
                     "template <class T>                      "
                     "void t(T t) { g(); }                    "
                     "template <>                             "
                     "void t(int t) {  }                      "
                     "void c() { t(13); }                     ";

  const std::string g = "_Z1gv", tInt = "_Z1tIiEvT_", c = "_Z1cv";

  TestCallGraph(data, {{g, {}}, {tInt, {}}, {c, {tInt}}});
}

TEST(BuildCallGraph, TemplatedClass) {

  std::string data = "void g() { }                            "
                     "template <class T>                      "
                     "class X {                               "
                     "public:                                 "
                     "  void x();                             "
                     "private:                                "
                     "  int * _m;                             "
                     "};                                      "
                     "template <class T>                      "
                     "void X<T>::x() { delete this->_m; g(); }"
                     "void c() { X<int> x; x.x(); }           ";

  const std::string g = "_Z1gv", xCons = "_ZN1XIiEC2Ev", x = "_ZN1XIiE1xEv",
                    c = "_Z1cv";

  TestCallGraph(data, {{g, {}}, {xCons, {}}, {x, {g}}, {c, {xCons, x}}});
}

TEST(BuildCallGraph, CRTP) {

  std::string data = "void g() { }                            "
                     "template <class Self>                   "
                     "class CRTP {                            "
                     "public:                                 "
                     "  Self *self() {                        "
                     "    return static_cast<Self *>(this);   "
                     "  }                                     "
                     "  void f() {                            "
                     "    Self *s = self();                   "
                     "    s->v();                             "
                     "  }                                     "
                     "};                                      "
                     "class Inst : public CRTP<Inst> {        "
                     "public:                                 "
                     "  void v() { g(); }                     "
                     "};                                      "
                     "void c(Inst I) { I.f(); }               ";

  const std::string g = "_Z1gv", self = "_ZN4CRTPI4InstE4selfEv",
                    f = "_ZN4CRTPI4InstE1fEv", v = "_ZN4Inst1vEv",
                    c = "_Z1c4Inst";

  TestCallGraph(data,
                {{g, {}}, {self, {}}, {f, {self, v}}, {v, {g}}, {c, {f}}});
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
