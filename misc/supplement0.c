#include <stdio.h>
#include <stdlib.h>

int GLOBAL_VAR = 3;

int ptrdiff(void *p1, void *p2) { return (char *)p1 - (char *)p2; }

int *alloc4(int a, int b, int c, int d)
{
	int *p = calloc(4, sizeof(int));
	p[0] = a;
	p[1] = b;
	p[2] = c;
	p[3] = d;
	return p;
}

int foobar(const char *str, int i, int j)
{
	printf(str, i, j);
	return 0;
}

struct INT_CHAR_CHAR_INT {
	int a;
	char c;
	char d;
	int b;
};

struct INT_CHAR_CHAR_INT *get_struct_pointer(int a, int b)
{
	struct INT_CHAR_CHAR_INT *p = calloc(1, sizeof(struct INT_CHAR_CHAR_INT));
	p->a = a;
	p->b = b;
	return p;
}

int add8(int a, int b, int c, int d, int e, int f, int g, int h)
{
	return a + b + c + d + e + f + g + h;
}

struct INT_CHARS_INT {
	int a;
	char c[100];
	int b;
};

struct INT_CHARS_INT merge7(int a, int b, int c, int d, int e, int f, int g
                            )
{
	struct INT_CHARS_INT st;
	st.a = a + b;
	st.b = c + d;
	st.c[0] = e + f;
	st.c[1] = g;
	return st;
}
