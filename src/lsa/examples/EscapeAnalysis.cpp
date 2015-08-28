#include "EscapeAnalysis.h"

namespace ohmu {
namespace lsa {

std::vector<unsigned>
EscapeTraversal::escapedParameter(ohmu::til::SExpr *Expr) {
  std::vector<unsigned> Escaped;
  // Using a stack to store expressions to check (could be multiple due to phi
  // nodes).
  std::vector<ohmu::til::SExpr *> CheckStack;
  CheckStack.emplace_back(Expr);

  while (!CheckStack.empty()) {
    ohmu::til::SExpr *E = CheckStack.back();
    CheckStack.pop_back();
    if (!E)
      continue;

    if (auto *Project = ohmu::dyn_cast<ohmu::til::Project>(E)) {
      CheckStack.emplace_back(Project->record());
    } else if (auto *ArrayIndex = ohmu::dyn_cast<ohmu::til::ArrayIndex>(E)) {
      CheckStack.emplace_back(ArrayIndex->array());
    } else if (auto *ArrayAdd = ohmu::dyn_cast<ohmu::til::ArrayAdd>(E)) {
      CheckStack.emplace_back(ArrayAdd->array());
    } else if (auto *Phi = ohmu::dyn_cast<ohmu::til::Phi>(E)) {
      for (ohmu::til::SExprRef &Expr : Phi->values()) {
        CheckStack.emplace_back(Expr.get());
      }
    } else if (auto *Parameter = ohmu::dyn_cast<ohmu::til::Variable>(E)) {
      unsigned p = Parameter->variableDecl()->varIndex();
      assert(p > 0);
      assert(p <= NParameters);
      Escaped.emplace_back(p);
    }
  }
  return Escaped;
}

void EscapeTraversal::reduceStore(ohmu::til::Store *E) {
  for (unsigned p : escapedParameter(E->source())) {
    Escaped->at(p) = true;
    InstructionEscapes[p].emplace(E->instrID());
  }
}

void EscapeTraversal::reduceReturn(ohmu::til::Return *E) {
  for (unsigned p : escapedParameter(E->returnValue())) {
    Escaped->at(p) = true;
    InstructionEscapes[p].emplace(E->instrID());
  }
}

void EscapeTraversal::reduceCall(ohmu::til::Call *E) {
  auto CallArguments = E->arguments();
  ohmu::til::SExpr *Callee = CallArguments.first;
  std::vector<ohmu::til::SExpr *> &Arguments = CallArguments.second;

  if (auto *Projection = ohmu::dyn_cast<ohmu::til::Project>(Callee)) {
    // We are calling a known function, register any parameter forwarding.
    string FunctionID = Projection->slotName().str();

    for (unsigned i = 0; i < Arguments.size(); i++) {
      for (unsigned p : escapedParameter(Arguments[i])) {
        // Note, argument count stars at 0, but parameter index starts at 1.
        ParameterAsArgument->at(p)
            .emplace_back(ArgumentInfo{FunctionID, i + 1, E->instrID()});
      }
    }

  } else {
    // Function unknown (e.g. passed as parameter), assume all parameter
    // arguments escape.

    for (ohmu::til::SExpr *Argument : Arguments) {
      for (unsigned p : escapedParameter(Argument)) {
        Escaped->at(p) = true;
        InstructionEscapes[p].emplace(E->instrID());
      }
    }
  }
}

void EscapeAnalysis::computePhase(GraphVertex *Vertex, const string &Phase,
                                  MessageList Messages) {

  if (!Vertex->value().Initialized) {
    initialize(Vertex);
  }

  bool updated = false;
  for (const Message &In : Messages) {
    for (unsigned i = 1; i < In.value().size(); ++i) {
      if (In.value()[i]) {
        // Parameter i at In.source() escapes; check if we pass one of our
        // parameters to that function in that position.
        updated = updated || updateEscapeData(Vertex, In.source(), i);
      }
    }
  }

  // In the first step we always inform callers what parameters escape. In later
  // steps only if some of the escape-status of parameters has changed.
  if (updated || stepCount() == 0) {
    for (const string &Out : Vertex->incomingCalls())
      Vertex->sendMessage(Out, Vertex->value().Escapes);
  }

  // Always vote to halt; we are awakened by incoming messages only.
  Vertex->voteToHalt();
}

void EscapeAnalysis::initialize(GraphVertex *Vertex) {
  unsigned NParameters = processParameters(Vertex);

  Vertex->mutableValue()->ParameterCount = NParameters;
  Vertex->mutableValue()->Escapes.resize(NParameters + 1);
  Vertex->mutableValue()->IsReference.resize(NParameters + 1);
  Vertex->mutableValue()->ParameterAsArgument.resize(NParameters + 1);
  Vertex->mutableValue()->EscapeLocations.resize(NParameters + 1);

  for (unsigned i = 1; i <= NParameters; i++) {
    if (Vertex->value().EscapeLocations[i].size() > 0)
      Vertex->mutableValue()->Escapes[i] = true;
  }

  escapeAnalysis(Vertex);

  Vertex->mutableValue()->Initialized = true;
}

string EscapeAnalysis::output(const GraphVertex *Vertex) const {
  std::stringstream ss;
  for (unsigned i = 1; i <= Vertex->value().ParameterCount; i++)
    ss << (Vertex->value().Escapes[i] ? "1" : "0");
  return ss.str();
}

unsigned EscapeAnalysis::processParameters(GraphVertex *Vertex) {
  ohmu::til::SExpr *IR = Vertex->ohmuIR();

  if (IR == nullptr)
    return 0;

  // Relying on ohmu generation:
  ohmu::til::Slot *S = cast<ohmu::til::Slot>(IR);
  if (ohmu::isa<ohmu::til::Code>(S->definition())) {
    return 0;
  }

  ohmu::til::Function *Func = cast<ohmu::til::Function>(S->definition());

  unsigned count = 1;
  updateIsReference(Vertex, Func->variableDecl()->definition(), count);
  while (Func->body() && Func->body()->opcode() == ohmu::til::COP_Function) {
    Func = cast<ohmu::til::Function>(Func->body());
    count += 1;
    updateIsReference(Vertex, Func->variableDecl()->definition(), count);
  }

  return count;
}

void EscapeAnalysis::updateIsReference(GraphVertex *Vertex, ohmu::til::SExpr *E,
                                       unsigned index) {
  // Possibly incorrect in the long run, but for now we consider a parameter a
  // reference if it is a pointer, or when it is not a scalar type.
  if (auto *Type = ohmu::dyn_cast<ohmu::til::ScalarType>(E)) {
    if (Type->baseType().isPointer()) {
      Vertex->mutableValue()->IsReference.resize(index + 1);
      Vertex->mutableValue()->IsReference[index] = true;
    }
  } else {
    Vertex->mutableValue()->IsReference.resize(index + 1);
    Vertex->mutableValue()->IsReference[index] = true;
  }
}

void EscapeAnalysis::escapeAnalysis(GraphVertex *Vertex) {
  if (Vertex->ohmuIR() == nullptr)
    return;

  EscapeTraversal Analyser(Vertex->value().ParameterCount,
                           &Vertex->mutableValue()->ParameterAsArgument,
                           &Vertex->mutableValue()->Escapes);
  Analyser.traverseAll(Vertex->ohmuIR());

  // Ignore non-reference/pointers 'escapes'
  for (unsigned p = 1; p <= Vertex->value().ParameterCount; p++) {
    if (!Vertex->value().IsReference[p]) {
      Vertex->mutableValue()->Escapes[p] = false;
    }
  }
}

bool EscapeAnalysis::updateEscapeData(GraphVertex *Vertex,
                                      const string &Function,
                                      unsigned ArgIndex) {
  bool updated = false;
  for (unsigned p = 1; p <= Vertex->value().ParameterCount; p++) {
    // We are only interested in parameters that are references/pointers.
    if (!Vertex->value().IsReference[p]) {
      continue;
    }

    for (const ArgumentInfo &Call : Vertex->value().ParameterAsArgument[p]) {
      if (Call.ArgumentPos == ArgIndex &&
          Function.compare(Call.FunctionName) == 0) {
        if (!Vertex->mutableValue()->Escapes[p]) {
          updated = true;
          Vertex->mutableValue()->Escapes[p] = true;
        }

        // Maintain the log of all the escape locations.
        Vertex->mutableValue()->EscapeLocations[p].emplace(Call.InstructionID);
      }
    }
  }

  return updated;
}

} // namespace lsa
} // namespace ohmu
