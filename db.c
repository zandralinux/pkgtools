/* See LICENSE file for copyright and license details. */
#include "pkg.h"

int fflag = 0;
int vflag = 0;

/* Initialize the db context */
struct db *
db_new(const char *prefix)
{
	struct db *db;
	struct sigaction sa;

	db = emalloc(sizeof(*db));
	TAILQ_INIT(&db->pkg_head);
	TAILQ_INIT(&db->pkg_rm_head);

	if (!realpath(prefix, db->prefix)) {
		weprintf("realpath %s:", prefix);
		free(db);
		return NULL;
	}

	estrlcpy(db->path, db->prefix, sizeof(db->path));
	estrlcat(db->path, DBPATH, sizeof(db->path));

	db->pkgdir = opendir(db->path);
	if (!db->pkgdir) {
		weprintf("opendir %s:", db->path);
		free(db);
		return NULL;
	}

	TAILQ_INIT(&db->rejrule_head);
	rej_load(db);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGHUP, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGQUIT, &sa, 0);
	sigaction(SIGTERM, &sa, 0);

	return db;
}

/* Free the db context and release resources */
int
db_free(struct db *db)
{
	struct pkg *pkg, *tmp;

	for (pkg = TAILQ_FIRST(&db->pkg_head); pkg; pkg = tmp) {
		tmp = TAILQ_NEXT(pkg, entry);
		pkg_free(pkg);
	}

	for (pkg = TAILQ_FIRST(&db->pkg_rm_head); pkg; pkg = tmp) {
		tmp = TAILQ_NEXT(pkg, entry);
		pkg_free(pkg);
	}

	closedir(db->pkgdir);
	rej_free(db);
	free(db);
	return 0;
}

/* Update the db entry on disk for the given package */
int
db_add(struct db *db, struct pkg *pkg)
{
	char path[PATH_MAX];
	char *name, *version;
	struct pkgentry *pe;
	FILE *fp;

	parse_name(pkg->path, &name);
	parse_version(pkg->path, &version);
	estrlcpy(path, db->path, sizeof(path));
	estrlcat(path, "/", sizeof(path));
	estrlcat(path, name, sizeof(path));
	if (version) {
		estrlcat(path, "#", sizeof(path));
		estrlcat(path, version, sizeof(path));
	}
	free(name);
	free(version);

	if (!(fp = fopen(path, "w"))) {
		weprintf("fopen %s:", path);
		return -1;
	}

	TAILQ_FOREACH(pe, &pkg->pe_head, entry) {
		if (vflag == 1)
			printf("installed %s\n", pe->path);
		fputs(pe->rpath, fp);
		fputc('\n', fp);
	}

	if (vflag == 1)
		printf("adding %s\n", path);
	fflush(fp);
	if (fsync(fileno(fp)) < 0)
		weprintf("fsync %s:", path);
	fclose(fp);

	return 0;
}

/* Physically unlink the db entry for the given package */
int
db_rm(struct db *db, struct pkg *pkg)
{
	struct pkg *tmp;

	TAILQ_FOREACH(tmp, &db->pkg_rm_head, entry) {
		if (strcmp(tmp->name, pkg->name) == 0) {
			if (vflag == 1)
				printf("removing %s\n", pkg->path);
			if (remove(pkg->path) < 0) {
				weprintf("remove %s:", pkg->path);
				return -1;
			}
			sync();
			return 0;
		}
	}
	weprintf("internal error: package not found on rm list");
	return -1;
}

/* Load all packages in the db */
int
db_load(struct db *db)
{
	struct pkg *pkg;
	struct dirent *dp;

	while ((dp = readdir(db->pkgdir))) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;
		pkg = pkg_load(db, dp->d_name);
		if (!pkg)
			return -1;
		TAILQ_INSERT_TAIL(&db->pkg_head, pkg, entry);
	}

	return 0;
}

/* Walk through all packages in the db and call `cb' for each one */
int
db_walk(struct db *db, int (*cb)(struct db *, struct pkg *, void *), void *data)
{
	struct pkg *pkg;
	int r;

	TAILQ_FOREACH(pkg, &db->pkg_head, entry) {
		r = cb(db, pkg, data);
		if (r < 0)
			return -1;
		/* terminate traversal */
		if (r > 0)
			return 1;
	}
	return 0;
}

/* Return the number of packages that have references to the given path */
int
db_links(struct db *db, const char *path)
{
	struct pkg *pkg;
	struct pkgentry *pe;
	int links = 0;

	TAILQ_FOREACH(pkg, &db->pkg_head, entry)
		TAILQ_FOREACH(pe, &pkg->pe_head, entry)
			if (strcmp(pe->path, path) == 0)
				links++;
	return links;
}
