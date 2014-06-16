/* See LICENSE file for copyright and license details. */
struct pkgentry {
	char path[PATH_MAX];
	struct pkgentry *next;
};

struct pkg {
	char *name;
	char *version;
	struct pkgentry *head;
};

extern int fflag;
extern int vflag;

struct db *dbinit(const char *);
int dbload(struct db *);
int dbfscollide(struct db *, const char *);
int dbadd(struct db *, const char *);
int dbwalk(struct db *, int (*)(struct db *, struct pkg *, void *), void *);
int dblinks(struct db *, const char *);
int dbfree(struct db *);
void dbdump(struct db *);
int dbpkgload(struct db *, struct pkg *);
int dbpkginstall(struct db *, const char *);
int dbpkgremove(struct db *, const char *);
int dbrm(struct db *, const char *);
struct pkg *pkgnew(char *);
void pkgfree(struct pkg *);
void parseversion(const char *, char **);
void parsename(const char *, char **);
