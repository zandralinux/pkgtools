/* See LICENSE file for copyright and license details. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "db.h"
#include "util.h"

static int removepkg(struct db *, struct pkg *, void *);

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-v] [-f] [-p prefix] pkg...\n", argv0);
	fprintf(stderr, "  -v    Enable verbose output\n");
	fprintf(stderr, "  -f    Force the removal of empty directories and symlinks\n");
	fprintf(stderr, "  -p    Set the installation prefix\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	struct db *db;
	char path[PATH_MAX];
	char *prefix = "/";
	int r;
	int i;

	ARGBEGIN {
	case 'v':
		vflag = 1;
		break;
	case 'f':
		fflag = 1;
		break;
	case 'p':
		prefix = ARGF();
		break;
	default:
		usage();
	} ARGEND;

	if (argc < 1)
		usage();

	db = dbinit(prefix);
	if (!db)
		exit(EXIT_FAILURE);
	r = dbload(db);
	if (r < 0)
		exit(EXIT_FAILURE);

	for (i = 0; i < argc; i++) {
		realpath(argv[i], path);
		r = dbwalk(db, removepkg, path);
		if (r < 0) {
			exit(EXIT_FAILURE);
		} else if (r > 0) {
			dbrm(db, path);
			printf("removed %s\n", path);
		} else {
			printf("%s is not installed\n", path);
		}
	}

	dbfree(db);

	return EXIT_SUCCESS;
}

static int
removepkg(struct db *db, struct pkg *pkg, void *file)
{
	char name[PATH_MAX], *p;

	estrlcpy(name, file, sizeof(name));
	p = basename(name);
	if (strcmp(pkg->name, p) == 0) {
		if (dbpkgremove(db, p) < 0)
			return -1;
		return 1;
	}
	return 0;
}
