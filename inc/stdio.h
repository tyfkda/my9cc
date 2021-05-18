#pragma once

#include "stdarg.h"
#include "stddef.h"
#include "sys/types.h"  // ssize_t

#define EOF  (-1)

enum {
  SEEK_SET,  // 0
  SEEK_CUR,  // 1
  SEEK_END,  // 2
};

typedef struct FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *fileName, const char *mode);
int fclose(FILE *fp);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *fp);
size_t fread(void *buffer, size_t size, size_t count, FILE *fp);
int fseek(FILE *fp, long offset, int origin);
long ftell(FILE *fp);
int remove(const char *fn);

int fgetc(FILE *fp);
int fputc(int c, FILE *fp);
int getchar(void);

int fprintf(FILE *fp, const char *fmt, ...);
int printf(const char *fmt, ...);
int sprintf(char *out, const char *fmt, ...);
int snprintf(char *, size_t n, const char *, ...);
int vfprintf(FILE *fp, const char *fmt, va_list ap);
int vsnprintf(char *out, size_t n, const char *fmt_, va_list ap);

void perror(const char *);

int fileno(FILE *fp);
FILE *tmpfile(void);

ssize_t getline(char **lineptr, size_t *n, FILE *stream);

int getc(FILE *fp);
char *fgets(char *s, int n, FILE *fp);
