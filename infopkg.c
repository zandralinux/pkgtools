/* See LICENSE file for copyright and license details. */
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "util.h"

static void ownpkg(const char *, const char *);

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
	DIR *dir;
	struct dirent *dp;
	char *prefix = "/";
	char path[PATH_MAX];
	int Oflag = 0;
	int lockfd;
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

	r = chdir(prefix);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", prefix,
			strerror(errno));
		return EXIT_FAILURE;
	}

	lockfd = lockdb();

	dir = opendir("var/pkg");
	if (!dir) {
		fprintf(stderr, "opendir %s: %s\n", "var/pkg",
			strerror(errno));
		return EXIT_FAILURE;
	}

	for (i = 0; i < argc; i++) {
		while ((dp = readdir(dir))) {
			if (strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0)
				continue;
			if (strlcpy(path, dp->d_name, sizeof(path)) >= sizeof(path)) {
				fprintf(stderr, "path too long\n");
				exit(EXIT_FAILURE);
			}
			ownpkg(basename(path), argv[i]);
		}
		rewinddir(dir);
	}

	closedir(dir);

	unlockdb(lockfd);

	return EXIT_SUCCESS;
}

static void
ownpkg(const char *pkg, const char *f)
{
	struct stat sb1, sb2;
	char buf[BUFSIZ], *p;
	char path[PATH_MAX], filename[PATH_MAX];
	FILE *fp;
	int r;

	for (; *f == '/'; f++)
		;
	if (strlcpy(filename, f, sizeof(filename)) >= sizeof(filename)) {
		fprintf(stderr, "path too long\n");
		exit(EXIT_FAILURE);
	}

	r = lstat(filename, &sb1);
	if (r < 0) {
		fprintf(stderr, "lstat %s: %s\n", f, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (S_ISREG(sb1.st_mode) == 0) {
		fprintf(stderr, "%s is not a regular file\n", filename);
		exit(EXIT_FAILURE);
	}

	if (strlcpy(path, "var/pkg/", sizeof(path)) >= sizeof(path)) {
		fprintf(stderr, "path too long\n");
		exit(EXIT_FAILURE);
	}
	if (strlcat(path, pkg, sizeof(path)) >= sizeof(path)) {
		fprintf(stderr, "path too long\n");
		exit(EXIT_FAILURE);
	}

	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "fopen %s: %s\n", path,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	while (fgets(buf, sizeof(buf), fp)) {
		p = strrchr(buf, '\n');
		if (p)
			*p = '\0';

		if (buf[0] == '\0') {
			fprintf(stderr, "nil file entry in %s, skipping\n", path);
			continue;
		}

		r = lstat(buf, &sb2);
		if (r < 0) {
			fprintf(stderr, "lstat %s: %s\n",
				buf, strerror(errno));
			continue;
		}

		if (sb1.st_dev == sb2.st_dev &&
		    sb1.st_ino == sb2.st_ino) {
			printf("/%s is owned by %s\n", filename, pkg);
			break;
		}
	}
	if (ferror(fp)) {
		fprintf(stderr, "I/O error while processing %s\n", path);
		exit(EXIT_FAILURE);
	}

	fclose(fp);
}
