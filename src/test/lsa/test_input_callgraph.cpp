/// basics

void globalFunction() { }

void globalFunctionWithArg(int i) { }

void callingGlobalFunction() {
  globalFunction();
}

void callingGlobalFunctionWithArg() {
  globalFunctionWithArg(3);
}

void multipleCalls() {
  globalFunction();
  globalFunction();
  globalFunctionWithArg(15);
  globalFunctionWithArg(100);
}

/// basics classes
class B {
public:
  void memberFunctionWithOneArg(int x) { }
};

void callingMemberFunctionWithOneArg() {
  B b;
  b.memberFunctionWithOneArg(15);
}

class A {
public:
  void bar() { globalFunction(); }
};

void callingViaPointer(A* a) {
  a->bar();
}

/// templated functions

template <class T>
void templatedFunction(T t) {
  globalFunction();
}

void callingTemplatedFunction() {
  templatedFunction<bool>(false);
  templatedFunction<int>(3);
}

template <class T>
void templatedFunctionCalling(T t) {
  t.memberFunctionWithOneArg(13);
}

void callingTemplatedFunctionCalling() {
  B b;
  templatedFunctionCalling(b);
}

/// Specializations
template <class T>
void specializeMe(T t) {
  globalFunction();
}

template <>
void specializeMe(int x) {
  globalFunctionWithArg(x);
}

void callSpecialInt() {
  specializeMe(13);
}


/// templated classes

template <class T>
class TemplatedClass {
public:
  T* getResultT() { globalFunction(); return 0; }
};

void callingTemplatedClass() {
  TemplatedClass<bool> Tbool;
  Tbool.getResultT();
  TemplatedClass<int> Tint;
  Tint.getResultT();
}

template <class Self>
class CRTP {
public:
  Self *self() { return static_cast<Self *>(this); }

  void CRTPFunction() {
    Self* s = self();
    s->semiVirtualFunction();
  }
};

class InstanceCRTP : public CRTP<InstanceCRTP> {
public:
  void semiVirtualFunction() {
    globalFunction();
  }
};

void callingInstanceCRTP(InstanceCRTP C) {
  C.CRTPFunction();
}

class C {
public:
  void end() {
    globalFunction();
  }
  ~C() {
    end();
  }
};

void constructDestruct() {
  C c;
}

template <typename _T>
class X {
public:
  void x();
private:
  int * _m;
};

template <class _T>
void X<_T>::x() {
  delete this->_m;
}

void barre() {
  X<int> x;
  x.x();
}

/// overwriting virtual

/// dynamic dispatch on arguments

/// different return types?

