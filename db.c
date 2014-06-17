/* See LICENSE file for copyright and license details. */
#define _XOPEN_SOURCE 500
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#include "db.h"
#include "util.h"

int fflag = 0;
int vflag = 0;

struct dbentry {
	struct pkg *pkg;
	int deleted;
	struct dbentry *next;
};

struct db {
	DIR *pkgdir;
	char prefix[PATH_MAX];
	char path[PATH_MAX];
	struct dbentry *head;
};

struct db *
dbinit(const char *prefix)
{
	struct db *db;
	struct sigaction sa;

	db = malloc(sizeof(*db));
	if (!db) {
		fprintf(stderr, "out of memory\n");
		return NULL;
	}
	db->head = NULL;

	realpath(prefix, db->prefix);

	estrlcpy(db->path, db->prefix, sizeof(db->path));
	estrlcat(db->path, "/var/pkg", sizeof(db->path));

	db->pkgdir = opendir(db->path);
	if (!db->pkgdir) {
		fprintf(stderr, "opendir %s: %s\n", db->path,
			strerror(errno));
		return NULL;
	}

	if (flock(dirfd(db->pkgdir), LOCK_EX | LOCK_NB) < 0) {
		if (errno == EWOULDBLOCK)
			fprintf(stderr, "package db already locked\n");
		else
			fprintf(stderr, "flock %s: %s\n", db->path,
				strerror(errno));
		return NULL;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGHUP, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGQUIT, &sa, 0);
	sigaction(SIGTERM, &sa, 0);

	return db;
}

int
dbload(struct db *db)
{
	struct dbentry *de;
	struct dirent *dp;

	while ((dp = readdir(db->pkgdir))) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;
		de = malloc(sizeof(*de));
		if (!de) {
			fprintf(stderr, "out of memory\n");
			return -1;
		}
		de->pkg = pkgnew(dp->d_name);
		if (!de->pkg)
			return -1;
		if (dbpkgload(db, de->pkg) < 0)
			return -1;
		de->next = db->head;
		db->head = de;
	}

	return 0;
}

struct pkg *
dbfind(struct db *db, const char *name)
{
	struct dbentry *de;

	for (de = db->head; de; de = de->next)
		if (de->deleted == 0 && strcmp(de->pkg->name, name) == 0)
			break;
	if (!de)
		return NULL;
	return de->pkg;
}

int
dbcollide(struct db *db, const char *name)
{
	char pkgpath[PATH_MAX];
	char path[PATH_MAX];
	struct archive *ar;
	struct archive_entry *entry;
	struct stat sb;
	int ok = 0;
	int r;

	realpath(name, pkgpath);

	ar = archive_read_new();

	archive_read_support_filter_gzip(ar);
	archive_read_support_format_tar(ar);

	r = archive_read_open_filename(ar, pkgpath, 10240);
	if (r < 0) {
		fprintf(stderr, "%s\n", archive_error_string(ar));
		return -1;
	}

	while (1) {
		r = archive_read_next_header(ar, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			fprintf(stderr, "%s\n", archive_error_string(ar));
			return -1;
		}
		estrlcpy(path, db->prefix, sizeof(path));
		estrlcat(path, "/", sizeof(path));
		estrlcat(path, archive_entry_pathname(entry), sizeof(path));
		if (access(path, F_OK) == 0) {
			r = stat(path, &sb);
			if (r < 0) {
				fprintf(stderr, "lstat %s: %s\n",
					archive_entry_pathname(entry),
					strerror(errno));
				return -1;
			}
			if (S_ISDIR(sb.st_mode) == 0) {
				fprintf(stderr, "%s exists\n", path);
				ok = -1;
			}
		}
	}

	archive_read_free(ar);

	return ok;
}

int
dbadd(struct db *db, const char *name)
{
	char pkgpath[PATH_MAX], tmppath[PATH_MAX];
	char path[PATH_MAX];
	FILE *fp;
	struct archive *ar;
	struct archive_entry *entry;
	int r;

	realpath(name, pkgpath);

	estrlcpy(tmppath, pkgpath, sizeof(tmppath));
	estrlcpy(path, db->path, sizeof(path));
	estrlcat(path, "/", sizeof(path));
	estrlcat(path, basename(tmppath), sizeof(path));

	ar = archive_read_new();

	archive_read_support_filter_gzip(ar);
	archive_read_support_format_tar(ar);

	r = archive_read_open_filename(ar, pkgpath, 10240);
	if (r < 0) {
		fprintf(stderr, "%s\n", archive_error_string(ar));
		return -1;
	}

	fp = fopen(path, "w");
	if (!fp) {
		fprintf(stderr, "fopen %s: %s\n", path,
			strerror(errno));
		return -1;
	}

	while (1) {
		r = archive_read_next_header(ar, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			fprintf(stderr, "%s\n", archive_error_string(ar));
			return -1;
		}
		estrlcpy(tmppath, db->prefix, sizeof(tmppath));
		estrlcat(tmppath, "/", sizeof(tmppath));
		estrlcat(tmppath, archive_entry_pathname(entry), sizeof(tmppath));
		if (vflag == 1)
			printf("installed %s\n", tmppath);
		fputs(archive_entry_pathname(entry), fp);
		fputc('\n', fp);
	}

	if (vflag == 1)
		printf("updating %s\n", path);
	fflush(fp);
	r = fsync(fileno(fp));
	if (r < 0)
		fprintf(stderr, "fsync: %s\n", strerror(errno));
	fclose(fp);

	archive_read_free(ar);

	return 0;
}

int
dbwalk(struct db *db, int (*cb)(struct db *, struct pkg *, void *), void *data)
{
	struct dbentry *de;
	int r;

	for (de = db->head; de; de = de->next) {
		if (de->deleted == 1)
			continue;
		r = cb(db, de->pkg, data);
		if (r < 0)
			return -1;
		/* terminate traversal */
		if (r > 0)
			return 1;
	}
	return 0;
}

int
dblinks(struct db *db, const char *path)
{
	struct dbentry *de;
	struct pkgentry *pe;
	int links = 0;

	for (de = db->head; de; de = de->next) {
		if (de->deleted == 1)
			continue;
		for (pe = de->pkg->head; pe; pe = pe->next)
			if (strcmp(pe->path, path) == 0)
				links++;
	}
	return links;
}

int
dbfree(struct db *db)
{
	struct dbentry *de, *tmp;

	de = db->head;
	while (de) {
		tmp = de->next;
		pkgfree(de->pkg);
		free(de);
		de = tmp;
	}
	if (flock(dirfd(db->pkgdir), LOCK_UN) < 0) {
		fprintf(stderr, "flock %s: %s\n", db->path,
			strerror(errno));
		return -1;
	}
	closedir(db->pkgdir);
	free(db);
	return 0;
}

void
dbdump(struct db *db)
{
	struct dbentry *de;

	for (de = db->head; de; de = de->next) {
		if (de->deleted == 1)
			continue;
		puts(de->pkg->name);
	}
}

int
dbpkgload(struct db *db, struct pkg *pkg)
{
	struct pkgentry *pe;
	FILE *fp;
	char path[PATH_MAX];
	char *buf = NULL, *p;
	size_t sz = 0;

	if (pkg->head)
		return 0;

	estrlcpy(path, db->path, sizeof(path));
	estrlcat(path, "/", sizeof(path));
	estrlcat(path, pkg->name, sizeof(path));

	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "fopen %s: %s\n", pkg->name,
			strerror(errno));
		return -1;
	}

	while (getline(&buf, &sz, fp) != -1) {
		p = strrchr(buf, '\n');
		if (p)
			*p = '\0';

		if (buf[0] == '\0') {
			fprintf(stderr, "malformed pkg file: %s\n",
				path);
			return -1;
		}

		pe = malloc(sizeof(*pe));
		if (!pe) {
			fprintf(stderr, "out of memory\n");
			return -1;
		}

		estrlcpy(pe->path, db->prefix, sizeof(pe->path));
		estrlcat(pe->path, "/", sizeof(pe->path));
		estrlcat(pe->path, buf, sizeof(pe->path));

		pe->next = pkg->head;
		pkg->head = pe;
	}
	if (ferror(fp)) {
		fprintf(stderr, "input error %s: %s\n", pkg->name,
			strerror(errno));
		fclose(fp);
		return -1;
	}

	free(buf);
	fclose(fp);
	return 0;
}

int
dbpkginstall(struct db *db, const char *name)
{
	char cwd[PATH_MAX];
	char pkgpath[PATH_MAX];
	struct archive *ar;
	struct archive_entry *entry;
	int flags;
	int r;

	realpath(name, pkgpath);

	ar = archive_read_new();

	archive_read_support_filter_gzip(ar);
	archive_read_support_format_tar(ar);

	r = archive_read_open_filename(ar, pkgpath, 10240);
	if (r < 0) {
		fprintf(stderr, "%s\n", archive_error_string(ar));
		return -1;
	}

	getcwd(cwd, sizeof(cwd));
	r = chdir(db->prefix);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", db->prefix,
			strerror(errno));
		return -1;
	}

	while (1) {
		r = archive_read_next_header(ar, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			fprintf(stderr, "%s: %s\n", archive_entry_pathname(entry),
				archive_error_string(ar));
			r = chdir(cwd);
			if (r < 0)
				fprintf(stderr, "chdir %s: %s\n", cwd, strerror(errno));
			return -1;
		}
		flags = ARCHIVE_EXTRACT_OWNER | ARCHIVE_EXTRACT_PERM |
			ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_UNLINK |
			ARCHIVE_EXTRACT_SECURE_NODOTDOT;
		r = archive_read_extract(ar, entry, flags);
		if (r != ARCHIVE_OK && r != ARCHIVE_WARN)
			fprintf(stderr, "%s: %s\n", archive_entry_pathname(entry),
				archive_error_string(ar));
	}

	archive_read_free(ar);

	r = chdir(cwd);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", cwd, strerror(errno));
		return -1;
	}

	return 0;
}

static int
rmemptydir(const char *f, const struct stat *sb, int typeflag,
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

int
dbpkgremove(struct db *db, const char *name)
{
	struct dbentry *de;
	struct pkg *pkg;
	struct pkgentry *pe;
	struct stat sb;
	char tmppath[PATH_MAX], *p;
	int r;

	estrlcpy(tmppath, name, sizeof(tmppath));
	p = basename(tmppath);

	for (de = db->head; de; de = de->next) {
		if (de->deleted == 1)
			continue;
		if (strcmp(de->pkg->name, p) == 0) {
			pkg = de->pkg;
			break;
		}
	}
	if (!de) {
		fprintf(stderr, "%s is not installed\n", name);
		return -1;
	}

	for (pe = pkg->head; pe; pe = pe->next) {
		r = lstat(pe->path, &sb);
		if (r < 0) {
			fprintf(stderr, "lstat %s: %s\n",
				pe->path, strerror(errno));
			continue;
		}

		if (S_ISDIR(sb.st_mode) == 1) {
			if (fflag == 0)
				printf("ignoring directory %s\n", pe->path);
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
		r = remove(pe->path);
		if (r < 0) {
			fprintf(stderr, "remove %s: %s\n", pe->path,
				strerror(errno));
			continue;
		}
	}

	if (fflag == 1) {
		/* prune empty directories as well */
		for (pe = pkg->head; pe; pe = pe->next) {
			if (dblinks(db, pe->path) > 1)
				continue;
			nftw(pe->path, rmemptydir, 1, FTW_DEPTH);
		}
	}

	de->deleted = 1;

	return 0;
}

int
dbrm(struct db *db, const char *name)
{
	struct dbentry *de;
	char path[PATH_MAX], tmpname[PATH_MAX], *p;
	int r;

	estrlcpy(tmpname, name, sizeof(tmpname));
	p = basename(tmpname);
	for (de = db->head; de; de = de->next) {
		if (de->deleted == 1 && strcmp(de->pkg->name, p) == 0) {
			estrlcpy(path, db->path, sizeof(path));
			estrlcat(path, "/", sizeof(path));
			estrlcat(path, de->pkg->name, sizeof(path));
			if (vflag == 1)
				printf("removing %s\n", path);
			/* nuke db entry for this package */
			r = remove(path);
			if (r < 0) {
				fprintf(stderr, "remove %s: %s\n", path,
					strerror(errno));
				return -1;
			}
			sync();
			break;
		}
	}
	return 0;
}

struct pkg *
pkgnew(char *name)
{
	struct pkg *pkg;

	pkg = malloc(sizeof(*pkg));
	if (!pkg) {
		fprintf(stderr, "out of memory\n");
		return NULL;
	}
	pkg->name = name;
	pkg->head = NULL;
	return pkg;
}

void
pkgfree(struct pkg *pkg)
{
	struct pkgentry *pe, *tmp;

	pe = pkg->head;
	while (pe) {
		tmp = pe->next;
		free(pe);
		pe = tmp;
	}
	free(pkg);
}
