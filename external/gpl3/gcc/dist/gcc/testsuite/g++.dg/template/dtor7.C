// PR c++/40373
// { dg-compile }

struct A;
namespace
{
  struct A;
}

struct B {};

template <typename T> void
foo (T t)
{
  t.~A ();	// { dg-error "does not match destructor name" }
}

void
bar ()
{
  foo (B ());	// { dg-bogus "instantiated from here" "" { xfail *-*-* } }
}
