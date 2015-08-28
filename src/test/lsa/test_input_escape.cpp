int *Global;
int GlobalCopy;

void EscapeBoth(int *inp1, int *inp2) {
  Global = inp1 + 1 -1;
  int *tmp = inp2;
  Global = tmp;
}

void EscapeFirstOnly(int *fst, int *snd) {
  EscapeBoth(fst, fst);
}

void EscapeSecondOnly(int *fst, int *snd) {
  EscapeFirstOnly(snd, fst);
}

class EscapeSelf {
private:
  EscapeSelf *Leak;
public:
  void escape() {
    Leak = this;
  }
  void callEscape() {
    escape();
  }
};


void SimpleNoEscapePointer(int *i) { }
void SimpleNoEscapeReference(int &i) { }
void SimpleNoEscapeCopy(int i) { Global = &i; }

void SimpleEscapePointer(int *i) { Global = i; }
void SimpleEscapeReference(int &i) { Global = &i; }

void NoEscapeDereference(int *p) {
  GlobalCopy = *p;
}

void EscapeReferenceDereference(int *p) {
  Global = &(*p);
}

void bar(int a) { }

int* foo(int *a, int *b) {
  int c = *a + *b;
  bar(*a + *b);
  bar(c);         // getting rid of compile warning
  return a;
}

int* phiTestBothReturn(int *a, int *b, bool c) {
  if (c) {
    return a;
  } else {
    return b;
  }
}

void phiTestBothBranch(int *a, int *b, bool c) {
  int *x;
  if (c) {
    x = a;
  } else {
    x = b;
  }
  Global = x;
}

int* phiTestSingleReturn(int *a, int *b, bool c) {
  if (c) {
    return b;
  } else {
    return b;
  }
}

void phiTestSingleBranch(int *a, int *b, bool c) {
  int *x;
  if (c) {
    x = a;
  } else {
    x = a;
  }
  Global = x;
}

// Code below would serve testing with lifetime of objects.

/*
class Collection {
public:
  int TotalAge;
};

class User {
private:
  int Age;
  char *Name;
  Collection *Copy;

public:
  void AddAge(Collection *C) {
    C->TotalAge += Age;
  }

  void SaveCollectionPointer(Collection *C) {
    Copy = C;
  }

  void SaveCollectionReference(Collection &C) {
    Copy = &C;
  }
};

void NoEscapeLocalCollection(User* U) {
  Collection C;
  U->AddAge(&C);
}

void EscapeLocalCollectionReference(User* U) {
  Collection C;
  U->SaveCollectionReference(C);
}

void EscapeLocalCollectionPointer(User* U) {
  Collection C;
  U->SaveCollectionPointer(&C);
}

void NoEscapeLocalUserLocalCollection() {
  User U;
  Collection C;
  U.SaveCollectionPointer(&C);
}
*/
