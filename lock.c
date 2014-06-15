/* See LICENSE file for copyright and license details. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* lock the package dabatase - assumes cwd is the root prefix */
void
lockdb(void)
{
	DIR *dir;

	dir = opendir("var/pkg");
	if (!dir) {
		fprintf(stderr, "opendir %s: %s\n", "var/pkg",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (flock(dirfd(dir), LOCK_EX | LOCK_NB) < 0) {
		if (errno == EWOULDBLOCK)
			fprintf(stderr, "package db already locked\n");
		else
			fprintf(stderr, "flock %s: %s\n", "var/pkg",
				strerror(errno));
		exit(EXIT_FAILURE);
	}
}
