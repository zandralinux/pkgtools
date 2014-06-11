/* See LICENSE file for copyright and license details. */
#include <stddef.h>
#include "arg.h"

#define LEN(x) (sizeof (x) / sizeof *(x))

extern char *argv0;

int lockdb(void);
void unlockdb(int);

#undef strlcat
size_t strlcat(char *, const char *, size_t);
#undef strlcpy
size_t strlcpy(char *, const char *, size_t);
