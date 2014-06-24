/* See LICENSE file for copyright and license details. */
#include "pkg.h"

/* Create a package from the given db entry.  e.g. /var/pkg/pkg#version */
struct pkg *
pkg_load(struct db *db, const char *file)
{
	char path[PATH_MAX], tmp[PATH_MAX], *p;
	char *name, *version;
	struct pkg *pkg;
	struct pkgentry *pe;
	FILE *fp;
	char *buf = NULL;
	size_t sz = 0;
	ssize_t len;

	estrlcpy(tmp, file, sizeof(tmp));
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

		pe = pkgentry_new(db, buf);
		TAILQ_INSERT_TAIL(&pkg->pe_head, pe, entry);
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

/* Create a package from a file.  e.g. /tmp/pkg#version.pkg.tgz */
struct pkg *
pkg_load_file(struct db *db, const char *file)
{
	char path[PATH_MAX];
	const char *tmp;
	char *name, *version;
	struct pkg *pkg;
	struct pkgentry *pe;
	struct archive *ar;
	struct archive_entry *entry;
	int r;

	if (!realpath(file, path)) {
		weprintf("realpath %s:", file);
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

		tmp = archive_entry_pathname(entry);
		if (strncmp(tmp, "./", 2) == 0)
			tmp += 2;

		if (tmp[0] == '\0')
			continue;

		pe = pkgentry_new(db, tmp);
		TAILQ_INSERT_TAIL(&pkg->pe_head, pe, entry);
	}

	archive_read_free(ar);

	return pkg;
}

/* Install the given package */
int
pkg_install(struct db *db, struct pkg *pkg)
{
	char cwd[PATH_MAX];
	struct archive *ar;
	struct archive_entry *entry;
	int flags, r;

	ar = archive_read_new();

	archive_read_support_filter_gzip(ar);
	archive_read_support_filter_bzip2(ar);
	archive_read_support_filter_xz(ar);
	archive_read_support_format_tar(ar);

	if (archive_read_open_filename(ar, pkg->path, ARCHIVEBUFSIZ) < 0) {
		weprintf("archive_read_open_filename %s: %s\n", pkg->path,
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

/* Remove the package entries for the given package */
int
pkg_remove(struct db *db, struct pkg *pkg)
{
	struct pkgentry *pe;
	struct stat sb;

	TAILQ_FOREACH_REVERSE(pe, &pkg->pe_head, pe_head, entry) {
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
		if (remove(pe->path) < 0)
			weprintf("remove %s:", pe->path);
	}

	if (fflag == 1) {
		/* prune empty directories as well */
		TAILQ_FOREACH_REVERSE(pe, &pkg->pe_head, pe_head, entry) {
			if (rej_match(db, pe->rpath) > 0)
				continue;
			if (db_links(db, pe->path) > 1)
				continue;
			nftw(pe->path, rm_empty_dir, 1, FTW_DEPTH);
		}
	}

	TAILQ_REMOVE(&db->pkg_head, pkg, entry);
	TAILQ_INSERT_TAIL(&db->pkg_rm_head, pkg, entry);

	return 0;
}

/* Check if the contents of the given package
 * collide with corresponding entries in the filesystem */
int
pkg_collisions(struct pkg *pkg)
{
	char resolvedpath[PATH_MAX];
	struct pkgentry *pe;
	struct stat sb;
	int ok = 0;

	TAILQ_FOREACH(pe, &pkg->pe_head, entry) {
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
	TAILQ_INIT(&pkg->pe_head);
	return pkg;
}

void
pkg_free(struct pkg *pkg)
{
	struct pkgentry *pe, *tmp;

	for (pe = TAILQ_FIRST(&pkg->pe_head); pe; pe = tmp) {
		tmp = TAILQ_NEXT(pe, entry);
		pkgentry_free(pe);
	}
	free(pkg->name);
	free(pkg->version);
	free(pkg);
}

struct pkgentry *
pkgentry_new(struct db *db, const char *file)
{
	struct pkgentry *pe;

	pe = emalloc(sizeof(*pe));
	estrlcpy(pe->path, db->prefix, sizeof(pe->path));
	estrlcat(pe->path, "/", sizeof(pe->path));
	estrlcat(pe->path, file, sizeof(pe->path));
	estrlcpy(pe->rpath, file, sizeof(pe->rpath));
	return pe;
}

void
pkgentry_free(struct pkgentry *pe)
{
	free(pe);
}
