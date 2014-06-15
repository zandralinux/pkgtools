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

char *argv0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-O filename...] [-p prefix]\n", argv0);
	fprintf(stderr, "  -O	 Look for the package that owns the given filename(s)\n");
	fprintf(stderr, "  -p	 Set the installation prefix\n");
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
	int r;

	realpath(file, path);

	r = lstat(path, &sb1);
	if (r < 0) {
		fprintf(stderr, "lstat %s: %s\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (dbpkgload(db, pkg) < 0)
		exit(EXIT_FAILURE);

	for (pe = pkg->head; pe; pe = pe->next) {
		r = lstat(pe->path, &sb2);
		if (r < 0) {
			fprintf(stderr, "lstat %s: %s\n",
				pe->path, strerror(errno));
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
