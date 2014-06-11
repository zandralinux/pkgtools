/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define LOCKPATH "var/pkg/.lock"

/* lock the package dabatase - assumes cwd is the root prefix */
int
lockdb(void)
{
	struct flock fl;
	int r;
	int fd;

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = getpid();

	fd = open(LOCKPATH, O_WRONLY | O_CREAT, 0600);
	if (fd < 0) {
		fprintf(stderr, "failed to create lockfile: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	r = fcntl(fd, F_SETLKW, &fl);
	if (r < 0) {
		fprintf(stderr, "failed to obtain lock: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	return fd;
}

/* unlock the package dabatase - assumes cwd is the root prefix */
void
unlockdb(int fd)
{
	struct flock fl;
	int r;

	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = getpid();

	r = fcntl(fd, F_SETLKW, &fl);
	if (r < 0) {
		fprintf(stderr, "failed to clear lock: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	close(fd);

	unlink(LOCKPATH);
}
