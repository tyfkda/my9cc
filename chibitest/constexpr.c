#include "test.h"

float g40 = 1.5;
double g41 = 0.0 ? 55 : (0, 1 + 1 * 5.0 / 2 * (double)2 * (int)2.0);

int main() {
  { enum { ten=1+2+3+4 }; ASSERT(10, (ten)); }
  { int i=0; switch(3) { case 5-2+0*3: i++; } ASSERT(1, (i)); }
  { int x[1+1]; ASSERT(8, (sizeof(x))); }
  { char x[8-2]; ASSERT(6, (sizeof(x))); }
  { char x[2*3]; ASSERT(6, (sizeof(x))); }
  { char x[12/4]; ASSERT(3, (sizeof(x))); }
  { char x[12%10]; ASSERT(2, (sizeof(x))); }
//  ASSERT(0b100, ({ char x[0b110&0b101]; sizeof(x); }));
//  ASSERT(0b111, ({ char x[0b110|0b101]; sizeof(x); }));
//  ASSERT(0b110, ({ char x[0b111^0b001]; sizeof(x); }));
  { char x[1<<2]; ASSERT(4, (sizeof(x))); }
  { char x[4>>1]; ASSERT(2, (sizeof(x))); }
  { char x[(1==1)+1]; ASSERT(2, (sizeof(x))); }
  { char x[(1!=1)+1]; ASSERT(1, (sizeof(x))); }
  { char x[(1<1)+1]; ASSERT(1, (sizeof(x))); }
  { char x[(1<=1)+1]; ASSERT(2, (sizeof(x))); }
  { char x[1?2:3]; ASSERT(2, (sizeof(x))); }
  { char x[0?2:3]; ASSERT(3, (sizeof(x))); }
  { char x[(1,3)]; ASSERT(3, (sizeof(x))); }
  { char x[!0+1]; ASSERT(2, (sizeof(x))); }
  { char x[!1+1]; ASSERT(1, (sizeof(x))); }
  { char x[~-3]; ASSERT(2, (sizeof(x))); }
//  { char x[(5||6)+1]; ASSERT(2, (sizeof(x))); }
//  { char x[(0||0)+1]; ASSERT(1, (sizeof(x))); }
//  { char x[(1&&1)+1]; ASSERT(2, (sizeof(x))); }
//  { char x[(1&&0)+1]; ASSERT(1, (sizeof(x))); }
  { char x[(int)3]; ASSERT(3, (sizeof(x))); }
  { char x[(char)0xffffff0f]; ASSERT(15, (sizeof(x))); }
  { char x[(short)0xffff010f]; ASSERT(0x10f, (sizeof(x))); }
  { char x[(int)0xfffffffffff+5]; ASSERT(4, (sizeof(x))); }
//  { char x[(int*)0+2]; ASSERT(8, (sizeof(x))); }
//  { char x[(int*)16-1]; ASSERT(12, (sizeof(x))); }
//  { char x[(int*)16-(int*)4]; ASSERT(3, (sizeof(x))); }

  { char x[(-1>>31)+5]; ASSERT(4, (sizeof(x))); }
  { char x[(unsigned char)0xffffffff]; ASSERT(255, (sizeof(x))); }
  { char x[(unsigned short)0xffff800f]; ASSERT(0x800f, (sizeof(x))); }
  { char x[(unsigned int)0xfffffffffff>>31]; ASSERT(1, (sizeof(x))); }
  { char x[(long)-1/((long)1<<62)+1]; ASSERT(1, (sizeof(x))); }
//  { char x[(unsigned long)-1/((long)1<<62)+1]; ASSERT(4, (sizeof(x))); }
  { char x[(unsigned)1<-1]; ASSERT(1, (sizeof(x))); }
  { char x[(unsigned)1<=-1]; ASSERT(1, (sizeof(x))); }

  ASSERT(1, g40==1.5);
  ASSERT(1, g41==11);

  printf("OK\n");
  return 0;
}
