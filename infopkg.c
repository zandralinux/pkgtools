/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "db.h"
#include "util.h"

static int ownpkg(struct db *, struct pkg *, void *);

static void
usage(void)
{
	fprintf(stderr, VERSION " (c) 2014 morpheus engineers\n");
	fprintf(stderr, "usage: %s [-p prefix] [-O filename...]\n", argv0);
	fprintf(stderr, "  -p	 Set the installation prefix\n");
	fprintf(stderr, "  -O	 Look for the packages that own the given filename(s)\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct db *db;
	char *prefix = "/";
	int Oflag = 0;
	int i;
	int r;

	ARGBEGIN {
	case 'O':
		Oflag = 1;
		break;
	case 'p':
		prefix = ARGF();
		break;
	default:
		usage();
	} ARGEND;

	if (Oflag == 0 || argc < 1)
		usage();

	db = dbinit(prefix);
	if (!db)
		exit(EXIT_FAILURE);
	r = dbload(db);
	if (r < 0)
		exit(EXIT_FAILURE);

	for (i = 0; i < argc; i++) {
		r = dbwalk(db, ownpkg, argv[i]);
		if (r < 0)
			exit(EXIT_FAILURE);
	}

	dbfree(db);

	return EXIT_SUCCESS;
}

static int
ownpkg(struct db *db, struct pkg *pkg, void *file)
{
	struct pkgentry *pe;
	struct stat sb1, sb2;
	char path[PATH_MAX];

	realpath(file, path);

	if (lstat(path, &sb1) < 0)
		eprintf("lstat %s:", path);

	if (dbpkgload(db, pkg) < 0)
		exit(EXIT_FAILURE);

	for (pe = pkg->head; pe; pe = pe->next) {
		if (lstat(pe->path, &sb2) < 0) {
			weprintf("lstat %s:", pe->path);
			continue;
		}
		if (sb1.st_dev == sb2.st_dev &&
		    sb1.st_ino == sb2.st_ino) {
			printf("%s is owned by %s\n", path, pkg->name);
			break;
		}
	}
	return 0;
}
