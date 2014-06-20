/* See LICENSE file for copyright and license details. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "db.h"
#include "util.h"

static int fscollidepkg(struct db *, struct pkg *, void *);

static void
usage(void)
{
	fprintf(stderr, VERSION " (c) 2014 morpheus engineers\n");
	fprintf(stderr, "usage: %s [-v] [-f] [-r path] pkg...\n", argv0);
	fprintf(stderr, "  -v    Enable verbose output\n");
	fprintf(stderr, "  -f    Override filesystem checks and force installation\n");
	fprintf(stderr, "  -r    Set alternative installation root\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	struct db *db;
	char path[PATH_MAX];
	char *prefix = "/";
	int i, r;

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
		if (!realpath(argv[i], path)) {
			weprintf("realpath %s:", argv[i]);
			continue;
		}
		if (vflag == 1)
			printf("installing %s\n", path);
		if (fflag == 0) {
			r = dbwalk(db, fscollidepkg, path);
			if (r < 0) {
				dbfree(db);
				exit(EXIT_FAILURE);
			}
		}
		dbadd(db, path);
		dbpkginstall(db, path);
		printf("installed %s\n", path);
	}

	dbfree(db);

	return EXIT_SUCCESS;
}

static int
fscollidepkg(struct db *db, struct pkg *pkg, void *file)
{
	(void) pkg;
	if (dbfscollide(db, file) < 0)
		return -1;
	return 0;
}
