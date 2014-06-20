/* See LICENSE file for copyright and license details. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "db.h"
#include "util.h"

static int collisions_cb(struct db *, struct pkg *, void *);

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

	db = db_attach(prefix);
	if (!db)
		exit(EXIT_FAILURE);
	r = db_load(db);
	if (r < 0) {
		db_detach(db);
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
			r = db_walk(db, collisions_cb, path);
			if (r < 0) {
				db_detach(db);
				printf("not installed %s\n", path);
				exit(EXIT_FAILURE);
			}
		}
		db_add(db, path);
		pkg_install(db, path);
		printf("installed %s\n", path);
	}

	db_detach(db);

	return EXIT_SUCCESS;
}

static int
collisions_cb(struct db *db, struct pkg *pkg, void *file)
{
	(void) pkg;
	if (db_collisions(db, file) < 0)
		return -1;
	return 0;
}
