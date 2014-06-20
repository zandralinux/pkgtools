/* See LICENSE file for copyright and license details. */
struct pkgentry {
	/* full path */
	char path[PATH_MAX];
	/* relative path */
	char rpath[PATH_MAX];
	struct pkgentry *next;
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

struct db *dbinit(const char *);
int dbload(struct db *);
int dbfscollide(struct db *, const char *);
int dbadd(struct db *, const char *);
int dbwalk(struct db *, int (*)(struct db *, struct pkg *, void *), void *);
int dblinks(struct db *, const char *);
int dbfree(struct db *);
int dbpkgload(struct db *, struct pkg *);
int dbpkginstall(struct db *, const char *);
int dbpkgremove(struct db *, const char *);
int dbrm(struct db *, const char *);
struct pkg *pkgnew(char *);
void pkgfree(struct pkg *);
void parseversion(const char *, char **);
void parsename(const char *, char **);
int rejmatch(struct db *, const char *);
