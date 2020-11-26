#include "test.h"

typedef int MyInt, MyInt2[4];
//typedef int;

int main() {
  { typedef int t; t x=1; ASSERT(1, (x)); }
  { typedef struct {int a;} t; t x; ASSERT(1, (x.a=1, x.a)); }
//  { typedef int t; t t=1; ASSERT(1, (t)); }
  { typedef struct {int a;} t; { typedef int t; } t x; ASSERT(2, (x.a=2, x.a)); }
  { typedef t; t x; ASSERT(4, (sizeof(x))); }
  { MyInt x=3; ASSERT(3, (x)); }
  { MyInt2 x; ASSERT(16, (sizeof(x))); }

  printf("OK\n");
  return 0;
}
