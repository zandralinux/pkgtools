/* See LICENSE file for copyright and license details. */

#include <regex.h>
#include <sys/types.h>

struct pkgentry {
	/* full path */
	char path[PATH_MAX];
	/* relative path */
	char rpath[PATH_MAX];
	struct pkgentry *next;
};

struct rejrule {
	regex_t preg;
	struct rejrule *next;
};

struct pkg {
	char *name;
	char *version;
	int deleted;
	struct pkgentry *head;
	struct pkg *next;
};

extern int fflag;
extern int vflag;

struct db *db_new(const char *);
int db_free(struct db *);
int db_add(struct db *, const char *);
int db_rm(struct db *, const char *);
int db_load(struct db *);
int db_walk(struct db *, int (*)(struct db *, struct pkg *, void *), void *);
int db_links(struct db *, const char *);
int db_collisions(struct db *, const char *);

int pkg_load(struct db *, struct pkg *);
int pkg_install(struct db *, const char *);
int pkg_remove(struct db *, struct pkg *);
struct pkg *pkg_new(char *);
void pkg_free(struct pkg *);

void parse_version(const char *, char **);
void parse_name(const char *, char **);

void rej_free(struct rejrule *);
struct rejrule * rej_load(const char *);
int rej_match(struct db *, const char *);
struct rejrule * rej_load(const char *);
