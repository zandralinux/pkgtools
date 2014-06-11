/* See LICENSE file for copyright and license details. */
#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "util.h"

static void checkdb(const char *);
static void updatedb(const char *, const char *);
static int collisions(const char *, const char *);
static int extract(const char *, const char *);

char *argv0;
static int vflag = 0;

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
	int r;
	int i;
	char cwd[PATH_MAX];
	char *prefix = "/";
	int fflag = 0;
	int lockfd;

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

	getcwd(cwd, sizeof(cwd));

	r = chdir(prefix);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", prefix,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	lockfd = lockdb();

	r = chdir(cwd);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", prefix,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	checkdb(prefix);
	for (i = 0; i < argc; i++) {
		if (vflag == 1)
			printf("installing %s\n", argv[i]);
		if (fflag == 0) {
			if (collisions(prefix, argv[i]) != 0) {
				fprintf(stderr,
					"aborting %s, use -f to override\n",
					argv[i]);
				exit(EXIT_FAILURE);
			}
		}
		updatedb(prefix, argv[i]);
		extract(prefix, argv[i]);
		printf("installed %s\n", argv[i]);
	}

	r = chdir(prefix);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", prefix,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	unlockdb(lockfd);

	return EXIT_SUCCESS;
}

static void
checkdb(const char *prefix)
{
	char cwd[PATH_MAX];
	char *dbpath = "var/pkg";
	int r;

	getcwd(cwd, sizeof(cwd));

	r = chdir(prefix);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", prefix,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	r = access(dbpath, R_OK | W_OK | X_OK);
	if (r < 0) {
		fprintf(stderr, "access %s: %s\n", dbpath, strerror(errno));
		exit(EXIT_FAILURE);
	}

	chdir(cwd);
}

static void
updatedb(const char *prefix, const char *f)
{
	char cwd[PATH_MAX];
	char path[PATH_MAX], filename[PATH_MAX];
	FILE *fp;
	struct archive *in;
	struct archive_entry *entry;
	int r;

	getcwd(cwd, sizeof(cwd));

	in = archive_read_new();

	archive_read_support_filter_gzip(in);
	archive_read_support_format_tar(in);

	r = archive_read_open_filename(in, f, BUFSIZ);
	if (r < 0) {
		fprintf(stderr, "%s\n", archive_error_string(in));
		exit(EXIT_FAILURE);
	}

	r = chdir(prefix);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", prefix,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (strlcpy(path, "var/pkg/", sizeof(path)) >= sizeof(path)) {
		fprintf(stderr, "path too long\n");
		exit(EXIT_FAILURE);
	}
	if (strlcpy(filename, f, sizeof(filename)) >= sizeof(filename)) {
		fprintf(stderr, "path too long\n");
		exit(EXIT_FAILURE);
	}
	if (strlcat(path, basename(filename), sizeof(path)) >= sizeof(path)) {
		fprintf(stderr, "path too long\n");
		exit(EXIT_FAILURE);
	}

	fp = fopen(path, "w");
	if (!fp) {
		fprintf(stderr, "fopen %s: %s\n", path,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	while (1) {
		r = archive_read_next_header(in, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			fprintf(stderr, "%s\n", archive_error_string(in));
			exit(EXIT_FAILURE);
		}
		if (vflag == 1)
			printf("installed %s\n", archive_entry_pathname(entry));
		fputs(archive_entry_pathname(entry), fp);
		fputc('\n', fp);
	}

	if (vflag == 1)
		printf("updating %s\n", path);
	fflush(fp);
	r = fsync(fileno(fp));
	if (r < 0)
		fprintf(stderr, "fsync: %s\n", strerror(errno));
	fclose(fp);

	archive_read_close(in);
	archive_read_free(in);

	chdir(cwd);
}

static int
collisions(const char *prefix, const char *f)
{
	char cwd[PATH_MAX];
	struct archive *in;
	struct archive_entry *entry;
	struct stat sb;
	int ok = 0;
	int r;

	getcwd(cwd, sizeof(cwd));

	in = archive_read_new();

	archive_read_support_filter_gzip(in);
	archive_read_support_format_tar(in);

	r = archive_read_open_filename(in, f, BUFSIZ);
	if (r < 0) {
		fprintf(stderr, "%s\n", archive_error_string(in));
		exit(EXIT_FAILURE);
	}

	r = chdir(prefix);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", prefix,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	while (1) {
		r = archive_read_next_header(in, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			fprintf(stderr, "%s\n", archive_error_string(in));
			exit(EXIT_FAILURE);
		}
		if (access(archive_entry_pathname(entry), F_OK) == 0) {
			r = stat(archive_entry_pathname(entry), &sb);
			if (r < 0) {
				fprintf(stderr, "lstat %s: %s\n",
					archive_entry_pathname(entry),
					strerror(errno));
				exit(EXIT_FAILURE);
			}
			if (S_ISDIR(sb.st_mode) == 0) {
				fprintf(stderr, "%s/%s exists\n", prefix,
					archive_entry_pathname(entry));
				ok = 1;
			}
		}
	}

	archive_read_close(in);
	archive_read_free(in);

	chdir(cwd);
	return ok;
}

static int
extract(const char *prefix, const char *f)
{
	char cwd[PATH_MAX];
	const void *buf;
	size_t size;
	int64_t offset;
	struct archive *in;
	struct archive *out;
	struct archive_entry *entry;
	int r;

	getcwd(cwd, sizeof(cwd));

	in = archive_read_new();
	out = archive_write_disk_new();

	archive_write_disk_set_options(out,
		ARCHIVE_EXTRACT_TIME |
		ARCHIVE_EXTRACT_PERM |
		ARCHIVE_EXTRACT_ACL |
		ARCHIVE_EXTRACT_FFLAGS |
		ARCHIVE_EXTRACT_SECURE_NODOTDOT);

	archive_read_support_filter_gzip(in);
	archive_read_support_format_tar(in);

	r = archive_read_open_filename(in, f, BUFSIZ);
	if (r < 0) {
		fprintf(stderr, "%s\n", archive_error_string(in));
		exit(EXIT_FAILURE);
	}

	r = chdir(prefix);
	if (r < 0) {
		fprintf(stderr, "chdir %s: %s\n", prefix,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	while (1) {
		r = archive_read_next_header(in, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			fprintf(stderr, "%s\n", archive_error_string(in));
			exit(EXIT_FAILURE);
		}
		r = archive_write_header(out, entry);
		if (r != ARCHIVE_OK) {
			fprintf(stderr, "%s\n", archive_error_string(out));
		} else {
			while (1) {
				r = archive_read_data_block(in, &buf,
					&size, &offset);
				if (r == ARCHIVE_EOF)
					break;
				if (r != ARCHIVE_OK)
					break;
				r = archive_write_data_block(out, buf, size,
					offset);
				if (r != ARCHIVE_OK)
					break;
			}
		}
	}

	archive_read_close(in);
	archive_read_free(in);
	archive_write_close(out);
	archive_write_free(out);

	chdir(cwd);
	return 0;
}
