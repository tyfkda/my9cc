#if 1
int main() { return 0; }
#else
extern int printf (const char *, ...);
extern void vide(void);
__asm__("vide: ret");

int main() {
    vide();
    printf ("okay\n");
    return 0;
}
#endif
