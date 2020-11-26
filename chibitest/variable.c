#include "test.h"

int g1, g2[4];
static int g3 = 3;

int main() {
  { int a; ASSERT(3, (a=3, a)); }
  { int a=3; ASSERT(3, (a)); }
  { int a=3; int z=5; ASSERT(8, (a+z)); }

  { int a=3; ASSERT(3, (a)); }
  { int a=3; int z=5; ASSERT(8, (a+z)); }
  { int a; int b; ASSERT(6, (a=b=3, a+b)); }
  { int foo=3; ASSERT(3, (foo)); }
  { int foo123=3; int bar=5; ASSERT(8, (foo123+bar)); }

  { int x; ASSERT(4, (sizeof(x))); }
  { int x; ASSERT(4, (sizeof x)); }
  { int *x; ASSERT(8, (sizeof(x))); }
  { int x[4]; ASSERT(16, (sizeof(x))); }
  { int x[3][4]; ASSERT(48, (sizeof(x))); }
  { int x[3][4]; ASSERT(16, (sizeof(*x))); }
  { int x[3][4]; ASSERT(4, (sizeof(**x))); }
  { int x[3][4]; ASSERT(5, (sizeof(**x) + 1)); }
  { int x[3][4]; ASSERT(5, (sizeof **x + 1)); }
  { int x[3][4]; ASSERT(4, (sizeof(**x + 1))); }
  { int x=1; ASSERT(4, (sizeof(x=2))); }
  { int x=1; ASSERT(1, (sizeof(x=2), x)); }

  ASSERT(0, g1);
  ASSERT(3, (g1=3, g1));
  ASSERT(0, (g2[0]=0, g2[1]=1, g2[2]=2, g2[3]=3, g2[0]));
  ASSERT(1, (g2[0]=0, g2[1]=1, g2[2]=2, g2[3]=3, g2[1]));
  ASSERT(2, (g2[0]=0, g2[1]=1, g2[2]=2, g2[3]=3, g2[2]));
  ASSERT(3, (g2[0]=0, g2[1]=1, g2[2]=2, g2[3]=3, g2[3]));

  ASSERT(4, sizeof(g1));
  ASSERT(16, sizeof(g2));

  { char x=1; ASSERT(1, (x)); }
  { char x=1; char y=2; ASSERT(1, (x)); }
  { char x=1; char y=2; ASSERT(2, (y)); }

  { char x; ASSERT(1, (sizeof(x))); }
  { char x[10]; ASSERT(10, (sizeof(x))); }

  { int x=2; { int x=3; } ASSERT(2, (x)); }
  { int x=2; { int x=3; } int y=4; ASSERT(2, (x)); }
  { int x=2; { x=3; } ASSERT(3, (x)); }

//  { int x; int y; char z; char *a=(char*)&y; char *b=&z; ASSERT(7, (b-a)); }
//  { int x; char y; int z; char *a=&y; char *b=(char*)&z; ASSERT(1, (b-a)); }

  { long x; ASSERT(8, (sizeof(x))); }
  { short x; ASSERT(2, (sizeof(x))); }

  { char *x[3]; ASSERT(24, (sizeof(x))); }
  { char (*x)[3]; ASSERT(8, (sizeof(x))); }
  { char (x); ASSERT(1, (sizeof(x))); }
  { char (x)[3]; ASSERT(3, (sizeof(x))); }
  { char (x[3])[4]; ASSERT(12, (sizeof(x))); }
  { char (x[3])[4]; ASSERT(4, (sizeof(x[0]))); }
  { char *x[3]; char y; ASSERT(3, (x[0]=&y, y=3, x[0][0])); }
  { char x[3]; char (*y)[3]=(char(*)[3])x; ASSERT(4, (y[0][0]=4, y[0][0])); }

  { void *x; }

  ASSERT(3, g3);

  printf("OK\n");
  return 0;
}
