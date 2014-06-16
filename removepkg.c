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
	fprintf(stderr, VERSION " (c) 2014 morpheus engineers\n");
	fprintf(stderr, "usage: %s [-v] [-f] [-r path] pkg...\n", argv0);
	fprintf(stderr, "  -v    Enable verbose output\n");
	fprintf(stderr, "  -f    Force the removal of empty directories and symlinks\n");
	fprintf(stderr, "  -r    Set alternative installation root\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	struct db *db;
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
	case 'r':
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
	if (r < 0) {
		dbfree(db);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < argc; i++) {
		r = dbwalk(db, removepkg, argv[i]);
		if (r < 0) {
			dbfree(db);
			exit(EXIT_FAILURE);
		} else if (r > 0) {
			dbrm(db, argv[i]);
			printf("removed %s\n", argv[i]);
		} else {
			printf("%s is not installed\n", argv[i]);
		}
	}

	dbfree(db);

	return EXIT_SUCCESS;
}

static int
removepkg(struct db *db, struct pkg *pkg, void *name)
{
	char *n = name;

	if (strcmp(pkg->name, n) == 0) {
		if (dbpkgremove(db, n) < 0)
			return -1;
		return 1;
	}
	return 0;
}
