/* See LICENSE file for copyright and license details. */

#include <regex.h>
#include <sys/types.h>
#include "queue.h"

struct pkgentry {
	/* full path */
	char path[PATH_MAX];
	/* relative path */
	char rpath[PATH_MAX];
	TAILQ_ENTRY(pkgentry) entry;
};

struct rejrule {
	regex_t preg;
	TAILQ_ENTRY(rejrule) entry;
};

struct pkg {
	char *name;
	char *version;
	char path[PATH_MAX];
	TAILQ_HEAD(pe_head, pkgentry) pe_head;
	TAILQ_ENTRY(pkg) entry;
};

extern int fflag;
extern int vflag;

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

void rej_free(struct db *);
int rej_load(struct db *);
int rej_match(struct db *, const char *);
