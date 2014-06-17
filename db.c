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

#define DBPATH "/var/pkg"
#define ARCHIVEBUFSIZ BUFSIZ

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

/* Request access to the db and initialize the context */
struct db *
dbinit(const char *prefix)
{
	struct db *db;
	struct sigaction sa;

	db = emalloc(sizeof(*db));
	db->head = NULL;

	if(!(realpath(prefix, db->prefix))) {
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

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGHUP, &sa, 0);
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGQUIT, &sa, 0);
	sigaction(SIGTERM, &sa, 0);

	return db;
}

/* Load the entire db in memory */
int
dbload(struct db *db)
{
	struct dbentry *de;
	struct dirent *dp;

	while ((dp = readdir(db->pkgdir))) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;
		de = emalloc(sizeof(*de));
		de->pkg = pkgnew(dp->d_name);
		if (dbpkgload(db, de->pkg) < 0) {
			pkgfree(de->pkg);
			free(de);
			return -1;
		}
		de->next = db->head;
		db->head = de;
	}

	return 0;
}

/* Check if the contents of package `file'
 * collide with corresponding entries in the filesystem */
int
dbfscollide(struct db *db, const char *file)
{
	char pkgpath[PATH_MAX];
	char path[PATH_MAX];
	struct archive *ar;
	struct archive_entry *entry;
	struct stat sb;
	int ok = 0, r;

	realpath(file, pkgpath);

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

	while (1) {
		r = archive_read_next_header(ar, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			weprintf("archive_read_next_header: %s\n",
				 archive_error_string(ar));
			return -1;
		}
		estrlcpy(path, db->prefix, sizeof(path));
		estrlcat(path, "/", sizeof(path));
		estrlcat(path, archive_entry_pathname(entry), sizeof(path));
		if (access(path, F_OK) == 0) {
			if (stat(path, &sb) < 0) {
				weprintf("lstat %s:", archive_entry_pathname(entry));
				return -1;
			}
			if (S_ISDIR(sb.st_mode) == 0) {
				weprintf("%s exists\n", path);
				ok = -1;
			}
		}
	}

	archive_read_free(ar);

	return ok;
}

/* Update the db entry on disk for package `file' */
int
dbadd(struct db *db, const char *file)
{
	char pkgpath[PATH_MAX], tmppath[PATH_MAX];
	char path[PATH_MAX];
	FILE *fp;
	struct archive *ar;
	struct archive_entry *entry;
	int r;

	realpath(file, pkgpath);

	estrlcpy(tmppath, pkgpath, sizeof(tmppath));
	estrlcpy(path, db->path, sizeof(path));
	estrlcat(path, "/", sizeof(path));
	estrlcat(path, basename(tmppath), sizeof(path));

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

	if (!(fp = fopen(path, "w"))) {
		weprintf("fopen %s:", path);
		return -1;
	}

	while (1) {
		r = archive_read_next_header(ar, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			weprintf("archive_read_next_header: %s\n",
				 archive_error_string(ar));
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
		printf("adding %s\n", path);
	fflush(fp);
	if (fsync(fileno(fp)) < 0)
		weprintf("fsync %s:", path);
	fclose(fp);

	archive_read_free(ar);

	return 0;
}

/* Walk through all the db entries and call `cb' for each one */
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

/* Return the number of packages that have references to `path' */
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

/* Free the db context and release resources */
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
		weprintf("flock %s:", db->path);
		return -1;
	}
	closedir(db->pkgdir);
	free(db);
	return 0;
}

/* Dump the db entries, for debugging purposes only */
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

/* Load the package contents for `pkg' */
int
dbpkgload(struct db *db, struct pkg *pkg)
{
	struct pkgentry *pe;
	FILE *fp;
	char path[PATH_MAX];
	char *buf = NULL;
	size_t sz = 0;
	ssize_t len;

	if (pkg->head)
		return 0;

	estrlcpy(path, db->path, sizeof(path));
	estrlcat(path, "/", sizeof(path));
	estrlcat(path, pkg->name, sizeof(path));

	if (!(fp = fopen(path, "r"))) {
		weprintf("fopen %s:", pkg->name);
		return -1;
	}

	while ((len = getline(&buf, &sz, fp)) != -1) {
		if (len > 0 && buf[len - 1] == '\n')
			buf[len - 1] = '\0';

		if (buf[0] == '\0') {
			weprintf("%s: malformed pkg file\n", path);
			free(buf);
			fclose(fp);
			return -1;
		}

		pe = emalloc(sizeof(*pe));
		estrlcpy(pe->path, db->prefix, sizeof(pe->path));
		estrlcat(pe->path, "/", sizeof(pe->path));
		estrlcat(pe->path, buf, sizeof(pe->path));
		pe->next = pkg->head;
		pkg->head = pe;
	}
	free(buf);
	if (ferror(fp)) {
		weprintf("%s: read error:", pkg->name);
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

/* Install the package `file' to disk */
int
dbpkginstall(struct db *db, const char *file)
{
	char cwd[PATH_MAX];
	char pkgpath[PATH_MAX];
	struct archive *ar;
	struct archive_entry *entry;
	int flags, r;

	realpath(file, pkgpath);

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

/* Remove the package entries for `file' */
int
dbpkgremove(struct db *db, const char *file)
{
	struct dbentry *de;
	struct pkg *pkg;
	struct pkgentry *pe;
	struct stat sb;
	char tmppath[PATH_MAX], *p;

	estrlcpy(tmppath, file, sizeof(tmppath));
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
		weprintf("can't find %s in pkg db\n", p);
		return -1;
	}

	for (pe = pkg->head; pe; pe = pe->next) {
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
			if (dblinks(db, pe->path) > 1)
				continue;
			nftw(pe->path, rmemptydir, 1, FTW_DEPTH);
		}
	}

	de->deleted = 1;

	return 0;
}

/* Physically unlink the db entry for `file' */
int
dbrm(struct db *db, const char *file)
{
	struct dbentry *de;
	char path[PATH_MAX], tmpname[PATH_MAX], *p;

	estrlcpy(tmpname, file, sizeof(tmpname));
	p = basename(tmpname);
	for (de = db->head; de; de = de->next) {
		if (de->deleted == 1 && strcmp(de->pkg->name, p) == 0) {
			estrlcpy(path, db->path, sizeof(path));
			estrlcat(path, "/", sizeof(path));
			estrlcat(path, de->pkg->name, sizeof(path));
			if (vflag == 1)
				printf("removing %s\n", path);
			if (remove(path) < 0) {
				weprintf("remove %s:", path);
				return -1;
			}
			sync();
			break;
		}
	}
	return 0;
}

/* Create a new package instance */
struct pkg *
pkgnew(char *name)
{
	struct pkg *pkg;

	pkg = emalloc(sizeof(*pkg));
	pkg->name = estrdup(name);
	pkg->head = NULL;
	return pkg;
}

/* Release `pkg' instance */
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
	free(pkg->name);
	free(pkg);
}
