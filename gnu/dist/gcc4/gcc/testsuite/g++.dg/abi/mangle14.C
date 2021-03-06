// { dg-do compile }
// { dg-options "-Wabi -fabi-version=1" }

struct A {
  template <typename T> int f ();
};

typedef int (A::*P)();

template <P> struct S {};

void g (S<&A::f<int> >) {} // { dg-warning "mangle" }
