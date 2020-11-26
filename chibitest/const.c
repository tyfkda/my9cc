#include "test.h"

int main() {
  { const x; }
  { int const x; }
  { const int x; }
//  { const int const const x; }
  { const x = 5; ASSERT(5, (x)); }
  { const x = 8; int *const y=&x; ASSERT(8, (*y)); }
  { const x = 6; ASSERT(6, (*(const * const)&x)); }

  printf("OK\n");
  return 0;
}
