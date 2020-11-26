#include "test.h"

int main() {
  { struct {int a; int b;} x; ASSERT(1, (x.a=1, x.b=2, x.a)); }
  { struct {int a; int b;} x; ASSERT(2, (x.a=1, x.b=2, x.b)); }
  { struct {char a; int b; char c;} x; ASSERT(1, (x.a=1, x.b=2, x.c=3, x.a)); }
  { struct {char a; int b; char c;} x; ASSERT(2, (x.b=1, x.b=2, x.c=3, x.b)); }
  { struct {char a; int b; char c;} x; ASSERT(3, (x.a=1, x.b=2, x.c=3, x.c)); }

  { struct {char a; char b;} x[3]; char *p=(char*)x; ASSERT(0, (p[0]=0, x[0].a)); }
  { struct {char a; char b;} x[3]; char *p=(char*)x; ASSERT(1, (p[1]=1, x[0].b)); }
  { struct {char a; char b;} x[3]; char *p=(char*)x; ASSERT(2, (p[2]=2, x[1].a)); }
  { struct {char a; char b;} x[3]; char *p=(char*)x; ASSERT(3, (p[3]=3, x[1].b)); }

  { struct {char a[3]; char b[5];} x; char *p=(char*)&x; ASSERT(6, (x.a[0]=6, p[0])); }
  { struct {char a[3]; char b[5];} x; char *p=(char*)&x; ASSERT(7, (x.b[0]=7, p[3])); }

  { struct { struct { char b; } a; } x; ASSERT(6, (x.a.b=6, x.a.b)); }

  { struct {int a;} x; ASSERT(4, (sizeof(x))); }
  { struct {int a; int b;} x; ASSERT(8, (sizeof(x))); }
  { struct {int a, b;} x; ASSERT(8, (sizeof(x))); }
  { struct {int a[3];} x; ASSERT(12, (sizeof(x))); }
  { struct {int a;} x[4]; ASSERT(16, (sizeof(x))); }
  { struct {int a[3];} x[2]; ASSERT(24, (sizeof(x))); }
  { struct {char a; char b;} x; ASSERT(2, (sizeof(x))); }
  { struct {} x; ASSERT(0, (sizeof(x))); }
  { struct {char a; int b;} x; ASSERT(8, (sizeof(x))); }
  { struct {int a; char b;} x; ASSERT(8, (sizeof(x))); }

  { struct t {int a; int b;} x; struct t y; ASSERT(8, (sizeof(y))); }
  { struct t {int a; int b;}; struct t y; ASSERT(8, (sizeof(y))); }
  { struct t {char a[2];}; { struct t {char a[4];}; } struct t y; ASSERT(2, (sizeof(y))); }
  { struct t {int x;}; int t=1; struct t y; ASSERT(3, (y.x=2, t+y.x)); }

  { struct t {char a;} x; struct t *y = &x; ASSERT(3, (x.a=3, y->a)); }
  { struct t {char a;} x; struct t *y = &x; ASSERT(3, (y->a=3, x.a)); }

  { struct {int a,b;} x,y; ASSERT(3, (x.a=3, y=x, y.a)); }
  { struct t {int a,b;}; struct t x; x.a=7; struct t y; struct t *z=&y; ASSERT(7, (*z=x, y.a)); }
  { struct t {int a,b;}; struct t x; x.a=7; struct t y, *p=&x, *q=&y; ASSERT(7, (*q=*p, y.a)); }
  { struct t {char a, b;} x, y; ASSERT(5, (x.a=5, y=x, y.a)); }

  { struct {int a,b;} x,y; ASSERT(3, (x.a=3, y=x, y.a)); }
  { struct t {int a,b;}; struct t x; x.a=7; struct t y; struct t *z=&y; ASSERT(7, (*z=x, y.a)); }
  { struct t {int a,b;}; struct t x; x.a=7; struct t y, *p=&x, *q=&y; ASSERT(7, (*q=*p, y.a)); }
  { struct t {char a, b;} x, y; ASSERT(5, (x.a=5, y=x, y.a)); }

  { struct t {int a; int b;} x; struct t y; ASSERT(8, (sizeof(y))); }
  { struct t {int a; int b;}; struct t y; ASSERT(8, (sizeof(y))); }

  { struct {char a; long b;} x; ASSERT(16, (sizeof(x))); }
  { struct {char a; short b;} x; ASSERT(4, (sizeof(x))); }

  { struct foo *bar; ASSERT(8, (sizeof(bar))); }
  { struct T *foo; struct T {int x;}; ASSERT(4, (sizeof(struct T))); }
  { struct T { struct T *next; int x; } a; struct T b; ASSERT(1, (b.x=1, a.next=&b, a.next->x)); }
  { typedef struct T T; struct T { int x; }; ASSERT(4, (sizeof(T))); }

  printf("OK\n");
  return 0;
}
