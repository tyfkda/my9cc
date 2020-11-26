#include "test.h"

int main() {
  { char x; ASSERT(1, (sizeof(x))); }
  { short int x; ASSERT(2, (sizeof(x))); }
  { int short x; ASSERT(2, (sizeof(x))); }
  { int x; ASSERT(4, (sizeof(x))); }
  { long int x; ASSERT(8, (sizeof(x))); }
  { int long x; ASSERT(8, (sizeof(x))); }

  { long long x; ASSERT(8, (sizeof(x))); }

//  { _Bool x; ASSERT(0, (x=0, x)); }
//  { _Bool x; ASSERT(1, (x=1, x)); }
//  { _Bool x; ASSERT(1, (x=2, x)); }
//  ASSERT(1, (_Bool)1);
//  ASSERT(1, (_Bool)2);
//  ASSERT(0, (_Bool)(char)256);

  printf("OK\n");
  return 0;
}
