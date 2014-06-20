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
dbinit(const char *prefix)
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
	db->rejrules = rejload(db->prefix);

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
	struct pkg *pkg;
	struct dirent *dp;

	while ((dp = readdir(db->pkgdir))) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0)
			continue;
		pkg = pkgnew(dp->d_name);
		if (dbpkgload(db, pkg) < 0) {
			pkgfree(pkg);
			return -1;
		}
		pkg->next = db->head;
		db->head = pkg;
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
	char resolvedpath[PATH_MAX];
	struct archive *ar;
	struct archive_entry *entry;
	struct stat sb;
	int ok = 0, r;

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
				if(realpath(path, resolvedpath))
					weprintf("%s exists\n", resolvedpath);
				else
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
	char pkgpath[PATH_MAX];
	char path[PATH_MAX];
	char tmp[PATH_MAX];
	char *name, *version;
	FILE *fp;
	struct archive *ar;
	struct archive_entry *entry;
	int r;

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

	parsename(pkgpath, &name);
	parseversion(pkgpath, &version);
	estrlcpy(path, db->path, sizeof(path));
	estrlcat(path, "/", sizeof(path));
	estrlcat(path, name, sizeof(path));
	if (version) {
		estrlcat(path, "#", sizeof(path));
		estrlcat(path, version, sizeof(path));
	}

	if (!(fp = fopen(path, "w"))) {
		weprintf("fopen %s:", path);
		free(name);
		free(version);
		return -1;
	}

	while (1) {
		r = archive_read_next_header(ar, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			weprintf("archive_read_next_header: %s\n",
				 archive_error_string(ar));
			free(name);
			free(version);
			return -1;
		}

		estrlcpy(tmp, db->prefix, sizeof(tmp));
		estrlcat(tmp, "/", sizeof(tmp));
		estrlcat(tmp, archive_entry_pathname(entry),
			 sizeof(tmp));

		if (vflag == 1)
			printf("installed %s\n", tmp);
		fputs(archive_entry_pathname(entry), fp);
		fputc('\n', fp);
	}

	free(name);
	free(version);

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
dblinks(struct db *db, const char *path)
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

/* Free the db context and release resources */
int
dbfree(struct db *db)
{
	struct pkg *pkg, *tmp;

	pkg = db->head;
	while (pkg) {
		tmp = pkg->next;
		pkgfree(pkg);
		pkg = tmp;
	}
	if (flock(dirfd(db->pkgdir), LOCK_UN) < 0) {
		weprintf("flock %s:", db->path);
		return -1;
	}
	closedir(db->pkgdir);
	rejfree(db->rejrules);
	free(db);
	return 0;
}

/* Load the package contents for `pkg' */
int
dbpkgload(struct db *db, struct pkg *pkg)
{
	char path[PATH_MAX];
	struct pkgentry *pe;
	FILE *fp;
	char *buf = NULL;
	size_t sz = 0;
	ssize_t len;

	if (pkg->head)
		return 0;

	estrlcpy(path, db->path, sizeof(path));
	estrlcat(path, "/", sizeof(path));
	estrlcat(path, pkg->name, sizeof(path));
	if (pkg->version) {
		estrlcat(path, "#", sizeof(path));
		estrlcat(path, pkg->version, sizeof(path));
	}

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
		estrlcpy(pe->rpath, buf, sizeof(pe->rpath));
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
		if (rejmatch(db, archive_entry_pathname(entry)) > 0) {
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
dbpkgremove(struct db *db, const char *name)
{
	struct pkg *pkg;
	struct pkgentry *pe;
	struct stat sb;

	for (pkg = db->head; pkg; pkg = pkg->next) {
		if (pkg->deleted == 1)
			continue;
		if (strcmp(pkg->name, name) == 0)
			break;
	}
	if (!pkg) {
		weprintf("can't find %s in pkg db\n", name);
		return -1;
	}

	for (pe = pkg->head; pe; pe = pe->next) {
		if (rejmatch(db, pe->rpath) > 0) {
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
			if (rejmatch(db, pe->rpath) > 0)
				continue;
			if (dblinks(db, pe->path) > 1)
				continue;
			nftw(pe->path, rmemptydir, 1, FTW_DEPTH);
		}
	}

	pkg->deleted = 1;

	return 0;
}

/* Physically unlink the db entry for `file' */
int
dbrm(struct db *db, const char *name)
{
	struct pkg *pkg;
	char path[PATH_MAX];

	for (pkg = db->head; pkg; pkg = pkg->next) {
		if (pkg->deleted == 1 && strcmp(pkg->name, name) == 0) {
			estrlcpy(path, db->path, sizeof(path));
			estrlcat(path, "/", sizeof(path));
			estrlcat(path, pkg->name, sizeof(path));
			if (pkg->version) {
				estrlcat(path, "#", sizeof(path));
				estrlcat(path, pkg->version,
					 sizeof(path));
			}
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
pkgnew(char *filename)
{
	struct pkg *pkg;
	char tmp[PATH_MAX], *p;

	estrlcpy(tmp, filename, sizeof(tmp));
	p = strchr(tmp, '#');
	if (p)
		*p = '\0';
	pkg = emalloc(sizeof(*pkg));
	pkg->name = estrdup(tmp);
	if (p)
		pkg->version = estrdup(p + 1);
	else
		pkg->version = NULL;
	pkg->deleted = 0;
	pkg->head = NULL;
	pkg->next = NULL;
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
	free(pkg->version);
	free(pkg);
}

void
parsename(const char *path, char **name)
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
parseversion(const char *path, char **version)
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
rejfree(struct rejrule *list)
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
rejload(const char *prefix)
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
			rejfree(list);
			free(buf);
			fclose(fp);
			return NULL;
		}

		/* append to list: first item? or append to previous rule */
		if(!list)
			list = next = rule;
		else
			next->next = rule;
		next = rule;
	}
	free(buf);
	if (ferror(fp)) {
		weprintf("%s: read error:", rejpath);
		fclose(fp);
		rejfree(list);
		return NULL;
	}
	fclose(fp);

	return list;
}

int
rejmatch(struct db *db, const char *file)
{
	int match = 0, r;
	struct rejrule *rule;

	/* Skip initial '.' of "./" */
	if (strncmp(file, "./", 2) == 0)
		file++;

	for(rule = db->rejrules; rule; rule = rule->next) {
		r = regexec(&(rule->preg), file, 0, NULL, 0);
		if (r != REG_NOMATCH) {
			match = 1;
			break;
		}
	}

	return match;
}
