static int hidden() { return 0; }
int always87() { return 87; }
int always8() { return 8; }
int add(int x, int y) { return x + y; }
int subtract(int x, int y) { return x - y; }

int qwerty(char a, char b)
{
	int d;
	d = 3;
	char c;
	c = a + d;
	return c * 4;
}
int qwer(char a, char b)
{
	int d;
	d = 3;
	char c;
	c = a + d;
	return c * b;
}

struct TWO_INTS {
	int a;
	int b;
};

int add_two_ints(struct TWO_INTS *p) { return p->a + p->b; }

struct INT_CHAR_CHAR_INT {
	int a;
	char c;
	char d;
	int b;
};

int add_two_ints2(struct INT_CHAR_CHAR_INT *p) { return p->a + p->b; }

int add6(int a, int b, int c, int d, int e, int f)
{
	return a + b + c + d + e + f;
}


struct Q{int a; int b; int *p;};

struct Q q(void) {struct Q u; u.a = 100; u.b = 74; u.p = 0; return u;}

int r(struct Q *p) {return p->a + p->b;}


struct Test{int a; int b; int *p; int *q; int *r; int *s;};

struct Test test_(int *s)
{
	struct Test u;
	u.a = 74;
	u.s = s;
	return u;
}

int add_3(int a){return 3 + a;}

void *return_fp(void){return add_3;}

int call_fp(void* q){int (*p)(int) = q;return (*p)(171);}
