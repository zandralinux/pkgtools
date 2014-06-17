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
	fprintf(stderr, "usage: %s [-f] [-p prefix] pkg...\n", argv0);
	fprintf(stderr, "  -v    Enable verbose output\n");
	fprintf(stderr, "  -f    Override filesystem checks and force installation\n");
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
		if (vflag == 1)
			printf("installing %s\n", path);
		if (fflag == 0) {
			r = dbwalk(db, fscollidepkg, path);
			if (r < 0)
				exit(EXIT_FAILURE);
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
