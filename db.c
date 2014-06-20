/* See LICENSE file for copyright and license details. */
#define _XOPEN_SOURCE 500
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#include "db.h"
#include "util.h"

#define DBPATH        "/var/pkg"
#define DBPATHREJECT  "/etc/pkgtools/reject.conf"
#define ARCHIVEBUFSIZ BUFSIZ

struct db {
	DIR *pkgdir;
	char prefix[PATH_MAX];
	char path[PATH_MAX];
	struct rejrule *rejrules;
	struct pkg *head;
};

int fflag = 0;
int vflag = 0;

/* Request access to the db and initialize the context */
struct db *
db_new(const char *prefix)
{
	struct db *db;
	struct sigaction sa;

	db = emalloc(sizeof(*db));
	db->head = NULL;

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

	if (flock(dirfd(db->pkgdir), LOCK_EX | LOCK_NB) < 0) {
		if (errno == EWOULDBLOCK)
			weprintf("package db already locked\n");
		else
			weprintf("flock %s:", db->path);
		free(db);
		return NULL;
	}
	db->rejrules = rej_load(db->prefix);

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

	pkg = db->head;
	while (pkg) {
		tmp = pkg->next;
		pkg_free(pkg);
		pkg = tmp;
	}
	if (flock(dirfd(db->pkgdir), LOCK_UN) < 0) {
		weprintf("flock %s:", db->path);
		return -1;
	}
	closedir(db->pkgdir);
	rej_free(db->rejrules);
	free(db);
	return 0;
}

/* Update the db entry on disk for package `pkg' */
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

	for (pe = pkg->head; pe; pe = pe->next) {
		if (vflag == 1)
			printf("installed %s\n", pe->path);
		fputs(pe->path, fp);
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

/* Physically unlink the db entry for `pkg' */
int
db_rm(struct pkg *pkg)
{
	if (pkg->deleted == 0)
		return -1;
	if (vflag == 1)
		printf("removing %s\n", pkg->path);
	if (remove(pkg->path) < 0) {
		weprintf("remove %s:", pkg->path);
		return -1;
	}
	sync();
	return 0;
}

/* Load the entire db in memory */
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
		pkg->next = db->head;
		db->head = pkg;
	}

	return 0;
}

/* Walk through all the db entries and call `cb' for each one */
int
db_walk(struct db *db, int (*cb)(struct db *, struct pkg *, void *), void *data)
{
	struct pkg *pkg;
	int r;

	for (pkg = db->head; pkg; pkg = pkg->next) {
		if (pkg->deleted == 1)
			continue;
		r = cb(db, pkg, data);
		if (r < 0)
			return -1;
		/* terminate traversal */
		if (r > 0)
			return 1;
	}
	return 0;
}

/* Return the number of packages that have references to `path' */
int
db_links(struct db *db, const char *path)
{
	struct pkg *pkg;
	struct pkgentry *pe;
	int links = 0;

	for (pkg = db->head; pkg; pkg = pkg->next) {
		if (pkg->deleted == 1)
			continue;
		for (pe = pkg->head; pe; pe = pe->next)
			if (strcmp(pe->path, path) == 0)
				links++;
	}
	return links;
}

/* Load the package contents for the given `filename' */
struct pkg *
pkg_load(struct db *db, const char *filename)
{
	char path[PATH_MAX], tmp[PATH_MAX], *p;
	char *name, *version;
	struct pkg *pkg;
	struct pkgentry *pe;
	FILE *fp;
	char *buf = NULL;
	size_t sz = 0;
	ssize_t len;

	estrlcpy(tmp, filename, sizeof(tmp));
	p = strchr(tmp, '#');
	if (p)
		*p = '\0';
	name = tmp;
	version = p ? p + 1 : NULL;

	estrlcpy(path, db->path, sizeof(path));
	estrlcat(path, "/", sizeof(path));
	estrlcat(path, name, sizeof(path));
	if (version) {
		estrlcat(path, "#", sizeof(path));
		estrlcat(path, version, sizeof(path));
	}

	pkg = pkg_new(path, name, version);

	if (!(fp = fopen(pkg->path, "r"))) {
		weprintf("fopen %s:", pkg->path);
		pkg_free(pkg);
		return NULL;
	}

	while ((len = getline(&buf, &sz, fp)) != -1) {
		if (len > 0 && buf[len - 1] == '\n')
			buf[len - 1] = '\0';

		if (buf[0] == '\0') {
			weprintf("%s: malformed pkg file\n", pkg->path);
			free(buf);
			fclose(fp);
			pkg_free(pkg);
			return NULL;
		}

		pe = emalloc(sizeof(*pe));
		estrlcpy(pe->path, db->prefix, sizeof(pe->path));
		estrlcat(pe->path, "/", sizeof(pe->path));
		estrlcat(pe->path, buf, sizeof(pe->path));
		estrlcpy(pe->rpath, buf, sizeof(pe->rpath));
		pe->next = pkg->head;
		pkg->head = pe;
	}
	free(buf);
	if (ferror(fp)) {
		weprintf("%s: read error:", pkg->name);
		fclose(fp);
		pkg_free(pkg);
		return NULL;
	}
	fclose(fp);
	return pkg;
}

struct pkg *
pkg_load_file(struct db *db, const char *filename)
{
	char path[PATH_MAX];
	char *name, *version;
	struct pkg *pkg;
	struct pkgentry *pe;
	struct archive *ar;
	struct archive_entry *entry;
	int r;

	if (!realpath(filename, path)) {
		weprintf("realpath %s:", filename);
		return NULL;
	}

	parse_name(path, &name);
	parse_version(path, &version);
	pkg = pkg_new(path, name, version);
	free(name);
	free(version);

	ar = archive_read_new();

	archive_read_support_filter_gzip(ar);
	archive_read_support_filter_bzip2(ar);
	archive_read_support_filter_xz(ar);
	archive_read_support_format_tar(ar);

	if (archive_read_open_filename(ar, pkg->path, ARCHIVEBUFSIZ) < 0) {
		weprintf("archive_read_open_filename %s: %s\n", pkg->path,
			 archive_error_string(ar));
		pkg_free(pkg);
		return NULL;
	}

	while (1) {
		r = archive_read_next_header(ar, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			weprintf("archive_read_next_header: %s\n",
				 archive_error_string(ar));
			pkg_free(pkg);
			return NULL;
		}
		pe = emalloc(sizeof(*pe));
		estrlcpy(pe->path, db->prefix, sizeof(pe->path));
		estrlcat(pe->path, "/", sizeof(pe->path));
		estrlcat(pe->path, archive_entry_pathname(entry), sizeof(pe->path));
		pe->next = pkg->head;
		pkg->head = pe;
	}

	archive_read_free(ar);

	return pkg;
}

/* Install the package `file' to disk */
int
pkg_install(struct db *db, const char *file)
{
	char cwd[PATH_MAX];
	char pkgpath[PATH_MAX];
	struct archive *ar;
	struct archive_entry *entry;
	int flags, r;

	if (!realpath(file, pkgpath)) {
		weprintf("realpath %s:", file);
		return -1;
	}

	ar = archive_read_new();

	archive_read_support_filter_gzip(ar);
	archive_read_support_filter_bzip2(ar);
	archive_read_support_filter_xz(ar);
	archive_read_support_format_tar(ar);

	if (archive_read_open_filename(ar, pkgpath, ARCHIVEBUFSIZ) < 0) {
		weprintf("archive_read_open_filename %s: %s\n", pkgpath,
			 archive_error_string(ar));
		return -1;
	}

	if (!getcwd(cwd, sizeof(cwd))) {
		weprintf("getcwd:");
		return -1;
	}
	if (chdir(db->prefix) < 0) {
		weprintf("chdir %s:", db->prefix);
		return -1;
	}

	while (1) {
		r = archive_read_next_header(ar, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			weprintf("archive_read_next_header %s: %s\n",
				 archive_entry_pathname(entry), archive_error_string(ar));
			if (chdir(cwd) < 0)
				weprintf("chdir %s:", cwd);
			return -1;
		}
		if (rej_match(db, archive_entry_pathname(entry)) > 0) {
			weprintf("rejecting %s\n", archive_entry_pathname(entry));
			continue;
		}
		flags = ARCHIVE_EXTRACT_OWNER | ARCHIVE_EXTRACT_PERM |
			ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_NODOTDOT;
		if (fflag == 1)
			flags |= ARCHIVE_EXTRACT_UNLINK;
		r = archive_read_extract(ar, entry, flags);
		if (r != ARCHIVE_OK && r != ARCHIVE_WARN)
			weprintf("archive_read_extract %s: %s\n",
				 archive_entry_pathname(entry), archive_error_string(ar));
	}

	archive_read_free(ar);

	if (chdir(cwd) < 0) {
		weprintf("chdir %s:", cwd);
		return -1;
	}

	return 0;
}

static int
rm_empty_dir(const char *f, const struct stat *sb, int typeflag,
	   struct FTW *ftwbuf)
{
	(void) sb;
	(void) ftwbuf;

	if (typeflag == FTW_DP) {
		if (vflag == 1)
			printf("removing %s\n", f);
		rmdir(f);
	}
	return 0;
}

/* Remove the package entries for `pkg' */
int
pkg_remove(struct db *db, struct pkg *pkg)
{
	struct pkgentry *pe;
	struct stat sb;

	if (pkg->deleted == 1)
		return 0;

	for (pe = pkg->head; pe; pe = pe->next) {
		if (rej_match(db, pe->rpath) > 0) {
			weprintf("rejecting %s\n", pe->rpath);
			continue;
		}

		if (lstat(pe->path, &sb) < 0) {
			weprintf("lstat %s:", pe->path);
			continue;
		}

		if (S_ISDIR(sb.st_mode) == 1) {
			if (fflag == 0)
				printf("ignoring directory %s\n", pe->path);
			/* We'll remove these further down in a separate pass */
			continue;
		}

		if (S_ISLNK(sb.st_mode) == 1) {
			if (fflag == 0) {
				printf("ignoring link %s\n", pe->path);
				continue;
			}
		}

		if (vflag == 1)
			printf("removing %s\n", pe->path);
		if (remove(pe->path) < 0) {
			weprintf("remove %s:", pe->path);
			continue;
		}
	}

	if (fflag == 1) {
		/* prune empty directories as well */
		for (pe = pkg->head; pe; pe = pe->next) {
			if (rej_match(db, pe->rpath) > 0)
				continue;
			if (db_links(db, pe->path) > 1)
				continue;
			nftw(pe->path, rm_empty_dir, 1, FTW_DEPTH);
		}
	}

	pkg->deleted = 1;

	return 0;
}

/* Check if the contents of package `pkg'
 * collide with corresponding entries in the filesystem */
int
pkg_collisions(struct pkg *pkg)
{
	char resolvedpath[PATH_MAX];
	struct pkgentry *pe;
	struct stat sb;
	int ok = 0;

	for (pe = pkg->head; pe; pe = pe->next) {
		if (access(pe->path, F_OK) == 0) {
			if (stat(pe->path, &sb) < 0) {
				weprintf("lstat %s:", pe->path);
				return -1;
			}
			if (S_ISDIR(sb.st_mode) == 0) {
				if (realpath(pe->path, resolvedpath))
					weprintf("%s exists\n", resolvedpath);
				else
					weprintf("%s exists\n", pe->path);
				ok = -1;
			}
		}
	}

	return ok;
}

/* Create a new package instance */
struct pkg *
pkg_new(const char *path, const char *name, const char *version)
{
	struct pkg *pkg;

	pkg = emalloc(sizeof(*pkg));
	pkg->name = estrdup(name);
	if (version)
		pkg->version = estrdup(version);
	else
		pkg->version = NULL;
	estrlcpy(pkg->path, path, sizeof(pkg->path));
	pkg->deleted = 0;
	pkg->head = NULL;
	pkg->next = NULL;
	return pkg;
}

/* Release `pkg' instance */
void
pkg_free(struct pkg *pkg)
{
	struct pkgentry *pe, *tmp;

	pe = pkg->head;
	while (pe) {
		tmp = pe->next;
		free(pe);
		pe = tmp;
	}
	free(pkg->name);
	free(pkg->version);
	free(pkg);
}

void
parse_name(const char *path, char **name)
{
	char tmp[PATH_MAX], filename[PATH_MAX], *p;

	estrlcpy(tmp, path, sizeof(tmp));
	estrlcpy(filename, basename(tmp), sizeof(filename));
	/* strip extension */
	p = strrchr(filename, '.');
	if (!p)
		goto err;
	*p = '\0';
	p = strrchr(filename, '.');
	if (!p)
		goto err;
	*p = '\0';
	/* extract name */
	p = strchr(filename, '#');
	if (p)
		*p = '\0';
	if (filename[0] == '\0')
		goto err;
	*name = estrdup(filename);
	return;
err:
	eprintf("%s: invalid package filename\n",
		path);
}

void
parse_version(const char *path, char **version)
{
	char tmp[PATH_MAX], filename[PATH_MAX], *p;

	estrlcpy(tmp, path, sizeof(tmp));
	estrlcpy(filename, basename(tmp), sizeof(filename));
	/* strip extension */
	p = strrchr(filename, '.');
	if (!p)
		goto err;
	*p = '\0';
	p = strrchr(filename, '.');
	if (!p)
		goto err;
	*p = '\0';
	/* extract version */
	p = strchr(filename, '#');
	if (!p) {
		*version = NULL;
		return;
	}
	p++;
	if (*p == '\0')
		goto err;
	*version = estrdup(p);
	return ;
err:
	eprintf("%s: invalid package filename\n",
		path);
}

void
rej_free(struct rejrule *list)
{
	struct rejrule *rule, *tmp;
	rule = list;
	while(rule) {
		tmp = rule->next;
		regfree(&(rule->preg));
		free(rule);
		rule = tmp;
	}
}

struct rejrule *
rej_load(const char *prefix)
{
	struct rejrule *rule, *next, *list = NULL;
	char rejpath[PATH_MAX];
	FILE *fp;
	char *buf = NULL;
	size_t sz = 0;
	ssize_t len;
	int r;

	estrlcpy(rejpath, prefix, sizeof(rejpath));
	estrlcat(rejpath, DBPATHREJECT, sizeof(rejpath));

	if (!(fp = fopen(rejpath, "r")))
		return NULL;

	while ((len = getline(&buf, &sz, fp)) != -1) {
		if (!len || buf[0] == '#' || buf[0] == '\n')
			continue; /* skip empty lines and comments. */
		if (len > 0 && buf[len - 1] == '\n')
			buf[len - 1] = '\0';

		/* copy and add regex */
		rule = emalloc(sizeof(*rule));
		rule->next = NULL;

		r = regcomp(&(rule->preg), buf, REG_NOSUB | REG_EXTENDED);
		if (r != 0) {
			regerror(r, &(rule->preg), buf, len);
			weprintf("invalid pattern: %s\n", buf);
			free(buf);
			fclose(fp);
			rej_free(list);
			return NULL;
		}

		/* append to list: first item? or append to previous rule */
		if (!list)
			list = next = rule;
		else
			next->next = rule;
		next = rule;
	}
	free(buf);
	if (ferror(fp)) {
		weprintf("%s: read error:", rejpath);
		fclose(fp);
		rej_free(list);
		return NULL;
	}
	fclose(fp);

	return list;
}

int
rej_match(struct db *db, const char *file)
{
	struct rejrule *rule;

	/* Skip initial '.' of "./" */
	if (strncmp(file, "./", 2) == 0)
		file++;

	for(rule = db->rejrules; rule; rule = rule->next) {
		if (regexec(&(rule->preg), file, 0, NULL, 0) != REG_NOMATCH)
			return 1;
	}

	return 0;
}
