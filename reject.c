/* See LICENSE file for copyright and license details. */
#include "pkg.h"

/* Release the pre-loaded regexes */
void
rej_free(struct db *db)
{
	struct rejrule *rule, *tmp;

	for (rule = TAILQ_FIRST(&db->rejrule_head); rule; rule = tmp) {
		tmp = TAILQ_NEXT(rule, entry);
		regfree(&rule->preg);
		free(rule);
	}
}

/* Parse reject.conf and pre-compute regexes */
int
rej_load(struct db *db)
{
	struct rejrule *rule;
	char rejpath[PATH_MAX];
	FILE *fp;
	char *buf = NULL;
	size_t sz = 0;
	ssize_t len;
	int r;

	estrlcpy(rejpath, db->prefix, sizeof(rejpath));
	estrlcat(rejpath, DBPATHREJECT, sizeof(rejpath));

	if (!(fp = fopen(rejpath, "r")))
		return -1;

	while ((len = getline(&buf, &sz, fp)) != -1) {
		if (!len || buf[0] == '#' || buf[0] == '\n')
			continue; /* skip empty lines and comments. */
		if (len > 0 && buf[len - 1] == '\n')
			buf[len - 1] = '\0';

		/* copy and add regex */
		rule = emalloc(sizeof(*rule));

		r = regcomp(&(rule->preg), buf, REG_NOSUB | REG_EXTENDED);
		if (r != 0) {
			regerror(r, &(rule->preg), buf, len);
			weprintf("invalid pattern: %s\n", buf);
			free(buf);
			fclose(fp);
			rej_free(db);
			return -1;
		}

		TAILQ_INSERT_TAIL(&db->rejrule_head, rule, entry);
	}
	free(buf);
	if (ferror(fp)) {
		weprintf("%s: read error:", rejpath);
		fclose(fp);
		rej_free(db);
		return -1;
	}
	fclose(fp);

	return 0;
}

/* Match pre-computed regexes against the given filename */
int
rej_match(struct db *db, const char *file)
{
	struct rejrule *rule;

	/* Skip initial '.' of "./" */
	if (strncmp(file, "./", 2) == 0)
		file++;

	TAILQ_FOREACH(rule, &db->rejrule_head, entry) {
		if (regexec(&rule->preg, file, 0, NULL, 0) != REG_NOMATCH)
			return 1;
	}

	return 0;
}
