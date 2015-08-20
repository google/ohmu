//===- test_input_scc.cpp --------------------------------------*- C++ --*-===//
// A simple file for testing call graph generation and SCC computation.
//
// Generated graph:
//
//  a  ---->  b  ---->  c  ---->  d  ---->  e
//  ^         ^         |         ^         |
//  |         |         |         |         |
//  |         |         |         v         |
//  \-------  f  <------/         g  <------/
//
// SCC #1: {a, b, c, f}
// SCC #2: {d, e, g}
//===----------------------------------------------------------------------===//

void a();
void b();
void c();
void d();
void e();
void f();
void g();

void a() { b(); }
void b() { c(); }
void c() { f(); d(); }
void d() { e(); g(); }
void e() { g(); }
void f() { a(); b(); }
void g() { d(); }
