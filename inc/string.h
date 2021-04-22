#pragma once

#include <stddef.h>  // size_t

size_t strlen(const char *s);
char* strchr(const char *s, int c);
char* strrchr(const char *s, int c);
char *strstr(const char *s1, const char *s2);
int strcmp(const char *p, const char *q);
int strncmp(const char *p, const char *q, size_t n);
char* strcpy(char *s, const char *t);
char* strncpy(char *s, const char *t, size_t n);

void* memcpy(void *dst, const void *src, size_t n);
void* memmove(void*, const void*, size_t);
void* memset(void* buf, int val, size_t size);
int memcmp(const void *buf1, const void *buf2, size_t n);

char *strstr(const char *s1, const char *s2);
size_t strspn(const char *s1, const char *s2);
char *strpbrk(const char *s1, const char *s2);
int strcoll(const char *s1, const char *s2);
char *strerror(int errnum);
void* memchr(const void *s, int c, size_t n);
