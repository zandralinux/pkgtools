/* See LICENSE file for copyright and license details. */
#define _XOPEN_SOURCE 500
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "util.h"

static int rmemptydir(const char *, const struct stat *, int, struct FTW *);
static int removepkg(const char *);

char *argv0;
static int fflag;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-f] [-p prefix] pkg...\n", argv0);
	fprintf(stderr, "  -f    Force the removal of empty directories and symlinks\n");
	fprintf(stderr, "  -p    Set the installation prefix\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	DIR *dir;
	struct dirent *dp;
	char filename[PATH_MAX];
	char *prefix = "/";
	int found = 0;
	int r;
	int i;

	ARGBEGIN {
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

	r = chdir(prefix);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", prefix,
			strerror(errno));
		return EXIT_FAILURE;
	}

	dir = opendir("var/pkg");
	if (!dir) {
		fprintf(stderr, "opendir %s: %s\n", "var/pkg",
			strerror(errno));
		return EXIT_FAILURE;
	}

	for (i = 0; i < argc; i++) {
		strlcpy(filename, argv[i], sizeof(filename));
		found = 0;

		while ((dp = readdir(dir))) {
			if (strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0)
				continue;
			if (strcmp(dp->d_name, basename(filename)) == 0) {
				if (removepkg(argv[i]) != 0)
					return EXIT_FAILURE;
				found = 1;
				break;
			}
		}
		if (found == 0) {
			fprintf(stderr, "package %s not installed\n", argv[i]);
			return EXIT_FAILURE;
		}

		rewinddir(dir);
	}

	closedir(dir);

	return EXIT_SUCCESS;
}

static int
rmemptydir(const char *f, const struct stat *sb, int typeflag,
	   struct FTW *ftwbuf)
{
	(void) sb;
	(void) ftwbuf;

	if (typeflag == FTW_DP)
		rmdir(f);
	return 0;
}

static int
removepkg(const char *f)
{
	FILE *fp;
	struct stat sb;
	char buf[BUFSIZ], *p;
	char path[PATH_MAX], filename[PATH_MAX];
	int r;

	strlcpy(path, "var/pkg/", sizeof(path));
	strlcpy(filename, f, sizeof(filename));
	strlcat(path, basename(filename), sizeof(path));

	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "fopen %s: %s\n", path,
			strerror(errno));
		return 1;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		p = strrchr(buf, '\n');
		if (p)
			*p = '\0';

		if (buf[0] == '\0') {
			fprintf(stderr, "nil file entry in %s, skipping\n",
				path);
			continue;
		}

		r = lstat(buf, &sb);
		if (r < 0) {
			fprintf(stderr, "stat %s: %s\n",
				buf, strerror(errno));
			continue;
		}

		if (S_ISDIR(sb.st_mode)) {
			if (fflag == 0)
				printf("ignoring directory %s\n", buf);
			continue;
		}

		if (S_ISLNK(sb.st_mode)) {
			if (fflag == 0) {
				printf("ignoring link %s\n", buf);
				continue;
			}
		}

		r = remove(buf);
		if (r < 0) {
			fprintf(stderr, "remove %s: %s\n", buf,
				strerror(errno));
			continue;
		}
	}
	if (ferror(fp)) {
		fprintf(stderr, "I/O error while processing %s\n", path);
		fclose(fp);
		return 1;
	}

	if (fflag == 1) {
		/* prune empty directories as well */
		rewind(fp);
		while (fgets(buf, sizeof(buf), fp)) {
			p = strrchr(buf, '\n');
			if (p)
				*p = '\0';
			if (buf[0] == '\0')
				continue;
			nftw(buf, rmemptydir, 1, FTW_DEPTH);
		}
		if (ferror(fp)) {
			fprintf(stderr, "I/O error while processing %s\n", path);
			fclose(fp);
			return 1;
		}
	}

	fclose(fp);

	/* nuke db entry for this package */
	r = remove(path);
	if (r < 0) {
		fprintf(stderr, "remove %s: %s\n", path,
			strerror(errno));
		return 1;
	}

	sync();
	return 0;
}
