#include "test.h"

int main() {
  { int x=3; ASSERT(3, (*&x)); }
  { int x=3; int *y=&x; int **z=&y; ASSERT(3, (**z)); }
//  { int x=3; int y=5; ASSERT(5, (*(&x+1))); }
//  { int x=3; int y=5; ASSERT(3, (*(&y-1))); }
//  { int x=3; int y=5; ASSERT(5, (*(&x-(-1)))); }
//  { int x=3; int *y=&x; ASSERT(5, (*y=5, x)); }
//  { int x=3; int y=5; ASSERT(7, (*(&x+1)=7, y)); }
//  { int x=3; int y=5; ASSERT(7, (*(&y-2+1)=7, x)); }
  { int x=3; ASSERT(5, ((&x+2)-&x+3)); }
  { int x, y; ASSERT(8, (x=3, y=5, x+y)); }
  { int x=3, y=5; ASSERT(8, (x+y)); }

  { int x[2]; int *y=(int*)&x; ASSERT(3, (*y=3, *x)); }

  { int x[3]; ASSERT(3, (*x=3, *(x+1)=4, *(x+2)=5, *x)); }
  { int x[3]; ASSERT(4, (*x=3, *(x+1)=4, *(x+2)=5, *(x+1))); }
  { int x[3]; ASSERT(5, (*x=3, *(x+1)=4, *(x+2)=5, *(x+2))); }

  { int x[2][3]; int *y=(int*)x; ASSERT(0, (*y=0, **x)); }
  { int x[2][3]; int *y=(int*)x; ASSERT(1, (*(y+1)=1, *(*x+1))); }
  { int x[2][3]; int *y=(int*)x; ASSERT(2, (*(y+2)=2, *(*x+2))); }
  { int x[2][3]; int *y=(int*)x; ASSERT(3, (*(y+3)=3, **(x+1))); }
  { int x[2][3]; int *y=(int*)x; ASSERT(4, (*(y+4)=4, *(*(x+1)+1))); }
  { int x[2][3]; int *y=(int*)x; ASSERT(5, (*(y+5)=5, *(*(x+1)+2))); }

  { int x[3]; ASSERT(3, (*x=3, x[1]=4, x[2]=5, *x)); }
  { int x[3]; ASSERT(4, (*x=3, x[1]=4, x[2]=5, *(x+1))); }
  { int x[3]; ASSERT(5, (*x=3, x[1]=4, x[2]=5, *(x+2))); }
  { int x[3]; ASSERT(5, (*x=3, x[1]=4, x[2]=5, *(x+2))); }
  { int x[3]; ASSERT(5, (*x=3, x[1]=4, 2[x]=5, *(x+2))); }

  { int x[2][3]; int *y=(int*)x; ASSERT(0, (y[0]=0, x[0][0])); }
  { int x[2][3]; int *y=(int*)x; ASSERT(1, (y[1]=1, x[0][1])); }
  { int x[2][3]; int *y=(int*)x; ASSERT(2, (y[2]=2, x[0][2])); }
  { int x[2][3]; int *y=(int*)x; ASSERT(3, (y[3]=3, x[1][0])); }
  { int x[2][3]; int *y=(int*)x; ASSERT(4, (y[4]=4, x[1][1])); }
  { int x[2][3]; int *y=(int*)x; ASSERT(5, (y[5]=5, x[1][2])); }

  printf("OK\n");
  return 0;
}
