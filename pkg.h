/* See LICENSE file for copyright and license details. */
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <ftw.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <unistd.h>
#include "queue.h"
#include "arg.h"

#define LEN(x) (sizeof (x) / sizeof *(x))

#define DBPATH        "/var/pkg"
#define DBPATHREJECT  "/etc/pkgtools/reject.conf"
#define ARCHIVEBUFSIZ BUFSIZ

struct pkgentry {
	/* full path */
	char path[PATH_MAX];
	/* relative path */
	char rpath[PATH_MAX];
	TAILQ_ENTRY(pkgentry) entry;
};

struct pkg {
	char *name;
	char *version;
	char path[PATH_MAX];
	TAILQ_HEAD(pe_head, pkgentry) pe_head;
	TAILQ_ENTRY(pkg) entry;
};

struct rejrule {
	regex_t preg;
	TAILQ_ENTRY(rejrule) entry;
};

struct db {
	DIR *pkgdir;
	char prefix[PATH_MAX];
	char path[PATH_MAX];
	TAILQ_HEAD(rejrule_head, rejrule) rejrule_head;
	TAILQ_HEAD(pkg_head, pkg) pkg_head;
};

/* db.c */
extern int fflag;
extern int vflag;

/* eprintf.c */
extern char *argv0;

/* db.c */
struct db *db_new(const char *);
int db_free(struct db *);
int db_add(struct db *, struct pkg *);
int db_rm(struct pkg *);
int db_load(struct db *);
struct pkg *pkg_load_file(struct db *, const char *);
int db_walk(struct db *, int (*)(struct db *, struct pkg *, void *), void *);
int db_links(struct db *, const char *);

struct pkg *pkg_load(struct db *, const char *);
int pkg_install(struct db *, struct pkg *);
int pkg_remove(struct db *, struct pkg *);
int pkg_collisions(struct pkg *);
struct pkg *pkg_new(const char *, const char *, const char *);
void pkg_free(struct pkg *);

void parse_version(const char *, char **);
void parse_name(const char *, char **);

/* reject.c */
void rej_free(struct db *);
int rej_load(struct db *);
int rej_match(struct db *, const char *);

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
