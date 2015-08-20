//===- test_input_global_vars.cpp ------------------------------*- C++ --*-===//
// A simple file for testing detecting changes to global variables.
//===----------------------------------------------------------------------===//

int globalVariable;

void changeGlobalVariable() {
  globalVariable = 10;
}

void dontChangeAnything() {

}

void changeLocalVariable() {
  int localVariable;
  localVariable = 10;
}

int changeGlobalVariableMoreCode(bool b) {
  if (b) {
    return 11;
  } else {
    int x = 0;
    while (x < 21) {
      ++x;
    }
    globalVariable = 10;
    return globalVariable;
  }
}

int dontChangeGlobalVariableMoreCode(bool b) {
  if (b) {
    return 11;
  } else {
    int x = 0;
    while (x < 21) {
      ++x;
    }
    return globalVariable;
  }
}

class MyClass {
public:
  int classVariable;
  void changeMemberVariable() {
    this->classVariable = 10;
  }
};

void changeMemberVariableOutside() {
  MyClass C;
  C.classVariable = 10;
}

void changeMemberVariableOutside(MyClass &C) {
  C.classVariable = 10;
}

void changeAPointer(int *p) {
  *p = 10;
}

void changeGlobalVariableViaPointerCall() {
  changeAPointer(&globalVariable);
}

void changeGlobalVariableViaPointer() {
  int *p = &globalVariable;
  *p = 10;
}

void changeLocalVariableViaPointer() {
  int localVariable = 3;
  int *p = &localVariable;
  *p = 10;
}

void changeGlobalVariableViaReference() {
  int &p = globalVariable;
  p = 10;
}

void changeLocalVariableViaReference() {
  int localVariable = 3;
  int &p = localVariable;
  p = 10;
}
