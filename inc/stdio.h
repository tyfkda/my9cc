#pragma once

#include "stdarg.h"
#include "stddef.h"
#include "sys/types.h"  // ssize_t

#define EOF  (-1)
#define BUFSIZ  (512)

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
FILE *freopen(const char *fileName, const char *mode, FILE *fp);
int fclose(FILE *fp);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *fp);
size_t fread(void *buffer, size_t size, size_t count, FILE *fp);
int fseek(FILE *fp, long offset, int origin);
long ftell(FILE *fp);
int remove(const char *fn);
int fflush(FILE *fp);
int feof(FILE *fp);
int ferror(FILE *fp);

int fgetc(FILE *fp);
int fputc(int c, FILE *fp);
int getc(FILE *fp);
int getchar(void);

int fprintf(FILE *fp, const char *fmt, ...);
int printf(const char *fmt, ...);
int sprintf(char *out, const char *fmt, ...);
int snprintf(char *, size_t n, const char *, ...);
int vfprintf(FILE *fp, const char *fmt, va_list ap);
int vsnprintf(char *out, size_t n, const char *fmt_, va_list ap);

void perror(const char *);

int fileno(FILE *fp);
char *fgets(char *s, int n, FILE *fp);
int fputs(const char *s, FILE *fp);
FILE *tmpfile(void);

ssize_t getline(char **lineptr, size_t *n, FILE *stream);

int ungetc(int c, FILE *stream);
void clearerr(FILE *fp);
int rename(const char *oldname, const char *newname);

#define	_IOFBF	0		/* setvbuf should set fully buffered */
#define	_IOLBF	1		/* setvbuf should set line buffered */
#define	_IONBF	2		/* setvbuf should set unbuffered */
int	 setvbuf(FILE *, char *, int, size_t);

#define L_tmpnam  (13)
char* tmpnam(char* s);
