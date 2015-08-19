//===- ClangCFGWalker.h ----------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_ANALYSIS_TIL_CLANGCFGWALKER_H
#define CLANG_ANALYSIS_TIL_CLANGCFGWALKER_H

#include "clang/Analysis/Analyses/PostOrderCFGView.h"

namespace clang {
namespace tilcpp {


// This class defines the interface of a clang CFG Visitor.
// CFGWalker will invoke the following methods.
// Note that methods are not virtual; the visitor is templatized.
class CFGVisitor {
  // Enter the CFG for Decl D, and perform any initial setup operations.
  void enterCFG(CFG *Cfg, const NamedDecl *D, const CFGBlock *First) {}

  // Enter a CFGBlock.
  void enterCFGBlock(const CFGBlock *B) {}

  // Returns true if this visitor implements handlePredecessor
  bool visitPredecessors() { return true; }

  // Process a predecessor edge.
  void handlePredecessor(const CFGBlock *Pred) {}

  // Process a successor back edge to a previously visited block.
  void handlePredecessorBackEdge(const CFGBlock *Pred) {}

  // Called just before processing statements.
  void enterCFGBlockBody(const CFGBlock *B) {}

  // Process an ordinary statement.
  void handleStatement(const Stmt *S) {}

  // Process a destructor call
  void handleDestructorCall(const Expr *E, const CXXDestructorDecl *Dd) {}
  void handleDestructorCall(const VarDecl *Vd, const CXXDestructorDecl *Dd) {}

  // Called after all statements have been handled.
  void exitCFGBlockBody(const CFGBlock *B) {}

  // Return true
  bool visitSuccessors() { return true; }

  // Process a successor edge.
  void handleSuccessor(const CFGBlock *Succ) {}

  // Process a successor back edge to a previously visited block.
  void handleSuccessorBackEdge(const CFGBlock *Succ) {}

  // Leave a CFGBlock.
  void exitCFGBlock(const CFGBlock *B) {}

  // Leave the CFG, and perform any final cleanup operations.
  void exitCFG(const CFGBlock *Last) {}
};


// Walks the clang CFG, and invokes methods on a given CFGVisitor.
class ClangCFGWalker {
public:
  ClangCFGWalker() : CFGraph(nullptr), ACtx(nullptr), SortedGraph(nullptr) {}

  // Initialize the CFGWalker.  This setup only needs to be done once, even
  // if there are multiple passes over the CFG.
  bool init(AnalysisDeclContext &AC) {
    ACtx = &AC;
    CFGraph = AC.getCFG();
    if (!CFGraph)
      return false;

    // Ignore anonymous functions.
    if (!dyn_cast_or_null<NamedDecl>(AC.getDecl()))
      return false;

    SortedGraph = AC.getAnalysis<PostOrderCFGView>();
    if (!SortedGraph)
      return false;

    return true;
  }

  // Traverse the CFG, calling methods on V as appropriate.
  template <class Visitor>
  void walk(Visitor &V) {
    PostOrderCFGView::CFGBlockSet VisitedBlocks(CFGraph);

    V.enterCFG(CFGraph, getDecl(), &CFGraph->getEntry());

    for (const auto *CurrBlock : *SortedGraph) {
      VisitedBlocks.insert(CurrBlock);

      V.enterCFGBlock(CurrBlock);

      // Process predecessors, handling back edges last
      if (V.visitPredecessors()) {
        SmallVector<CFGBlock*, 4> BackEdges;
        // Process successors
        for (CFGBlock::const_pred_iterator SI = CurrBlock->pred_begin(),
                                           SE = CurrBlock->pred_end();
             SI != SE; ++SI) {
          if (*SI == nullptr)
            continue;

          if (!VisitedBlocks.alreadySet(*SI)) {
            BackEdges.push_back(*SI);
            continue;
          }
          V.handlePredecessor(*SI);
        }

        for (auto *Blk : BackEdges)
          V.handlePredecessorBackEdge(Blk);
      }

      V.enterCFGBlockBody(CurrBlock);

      // Process statements
      for (const auto &Bi : *CurrBlock) {
        switch (Bi.getKind()) {
        case CFGElement::Statement: {
          const CFGStmt* S = reinterpret_cast<const CFGStmt*>(&Bi);
          V.handleStatement(S->getStmt());
          break;
        }
        case CFGElement::DeleteDtor: {
          const CFGDeleteDtor* Dtor =
              reinterpret_cast<const CFGDeleteDtor*>(&Bi);
          CXXDestructorDecl *Dd = const_cast<CXXDestructorDecl*>(
              Dtor->getDestructorDecl(ACtx->getASTContext()));
          const Expr *E = Dtor->getDeleteExpr()->getArgument();
          V.handleDestructorCall(E, Dd);
          break;
        }
        case CFGElement::AutomaticObjectDtor: {
          const CFGAutomaticObjDtor* Dtor =
              reinterpret_cast<const CFGAutomaticObjDtor*>(&Bi);
          CXXDestructorDecl *Dd = const_cast<CXXDestructorDecl*>(
              Dtor->getDestructorDecl(ACtx->getASTContext()));
          VarDecl *Vd = const_cast<VarDecl*>(Dtor->getVarDecl());
          V.handleDestructorCall(Vd, Dd);
          break;
        }
        default:
          break;
        }
      }

      V.exitCFGBlockBody(CurrBlock);

      // Process successors, handling back edges first.
      if (V.visitSuccessors()) {
        SmallVector<CFGBlock*, 8> ForwardEdges;

        // Process successors
        for (CFGBlock::const_succ_iterator SI = CurrBlock->succ_begin(),
                                           SE = CurrBlock->succ_end();
             SI != SE; ++SI) {
          if (*SI == nullptr)
            continue;

          if (!VisitedBlocks.alreadySet(*SI)) {
            ForwardEdges.push_back(*SI);
            continue;
          }
          V.handleSuccessorBackEdge(*SI);
        }

        for (auto *Blk : ForwardEdges)
          V.handleSuccessor(Blk);
      }

      V.exitCFGBlock(CurrBlock);
    }
    V.exitCFG(&CFGraph->getExit());
  }

  const CFG *getGraph() const { return CFGraph; }
  CFG *getGraph() { return CFGraph; }

  const NamedDecl *getDecl() const {
    return dyn_cast<NamedDecl>(ACtx->getDecl());
  }

  const PostOrderCFGView *getSortedGraph() const { return SortedGraph; }

private:
  CFG *CFGraph;
  AnalysisDeclContext *ACtx;
  PostOrderCFGView *SortedGraph;
};


}  // end namespace clang
}  // end namespace tilcpp


#endif  // CLANG_ANALYSIS_TIL_CLANGCFGWALKER_H

