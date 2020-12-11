#include <stdio.h>
#include <stdlib.h>

#ifdef __XCC
#define CODE  __asm(\
    "  mov $0x1234ABCD, %eax\n" \
	"  ret\n")

unsigned short us(void) { CODE; }
short ss(void) { CODE; }
unsigned char uc(void) { CODE; }
signed char sc(void) { CODE; }
#else
asm (
    ".text;"
    ".globl _us;.globl _ss;.globl _uc;.globl _sc;"
    "_us:;_ss:;_uc:;_sc:;"
    "movl $0x1234ABCD, %eax;"
	"ret;"
);

#if 1
#define us _us
#define ss _ss
#define uc _uc
#define sc _sc
#endif
#endif

int main()
{
    unsigned short us(void);
    short ss(void);
    unsigned char uc(void);
    signed char sc(void);

    unsigned short (*fpus)(void) = us;
    short (*fpss)(void) = ss;
    unsigned char (*fpuc)(void) = uc;
    signed char (*fpsc)(void) = sc;

    printf("%08X %08X\n", us() + 1, fpus() + 1);
    printf("%08X %08X\n", ss() + 1, fpss() + 1);
    printf("%08X %08X\n", uc() + 1, fpuc() + 1);
    printf("%08X %08X\n", sc() + 1, fpsc() + 1);
    printf("\n");
    printf("%08X %08X\n", fpus() + 1, us() + 1);
    printf("%08X %08X\n", fpss() + 1, ss() + 1);
    printf("%08X %08X\n", fpuc() + 1, uc() + 1);
    printf("%08X %08X\n", fpsc() + 1, sc() + 1);

    return 0;
}
