#include "test.h"

int main() {
  { union { int a; char b[6]; } x; ASSERT(8, (sizeof(x))); }
  { union { int a; char b[4]; } x; ASSERT(3, (x.a = 515, x.b[0])); }
  { union { int a; char b[4]; } x; ASSERT(2, (x.a = 515, x.b[1])); }
  { union { int a; char b[4]; } x; ASSERT(0, (x.a = 515, x.b[2])); }
  { union { int a; char b[4]; } x; ASSERT(0, (x.a = 515, x.b[3])); }

  { union {int a,b;} x,y; ASSERT(3, (x.a=3, y.a=5, y=x, y.a)); }
  { union {struct {int a,b;} c;} x,y; ASSERT(3, (x.c.b=3, y.c.b=5, y=x, y.c.b)); }

  { union { struct { unsigned char a,b,c,d; }; long e; } x; ASSERT(0xef, (x.e=0xdeadbeef, x.a)); }
  { union { struct { unsigned char a,b,c,d; }; long e; } x; ASSERT(0xbe, (x.e=0xdeadbeef, x.b)); }
  { union { struct { unsigned char a,b,c,d; }; long e; } x; ASSERT(0xad, (x.e=0xdeadbeef, x.c)); }
  { union { struct { unsigned char a,b,c,d; }; long e; } x; ASSERT(0xde, (x.e=0xdeadbeef, x.d)); }

  {struct { union { int a,b; }; union { int c,d; }; } x; ASSERT(3, (x.a=3, x.b)); }
  {struct { union { int a,b; }; union { int c,d; }; } x; ASSERT(5, (x.d=5, x.c)); }

  printf("OK\n");
  return 0;
}
