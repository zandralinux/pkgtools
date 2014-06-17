/* See LICENSE file for copyright and license details. */
#include <stddef.h>
#include "arg.h"

#define LEN(x) (sizeof (x) / sizeof *(x))

extern char *argv0;

void lockdb(void);

/* ealloc.c */
void *ecalloc(size_t, size_t);
void *emalloc(size_t size);
void *erealloc(void *, size_t);
char *estrdup(const char *);

/* eprintf.c */
void enprintf(int, const char *, ...);
void eprintf(const char *, ...);
void weprintf(const char *, ...);

/* strlcat.c */
#undef strlcat
size_t strlcat(char *, const char *, size_t);
size_t estrlcat(char *, const char *, size_t);

/* strlcpy.c */
#undef strlcpy
size_t strlcpy(char *, const char *, size_t);
size_t estrlcpy(char *, const char *, size_t);
