#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/mman.h>
#include <getopt.h>
#include <sysexits.h>
#include <err.h>

#define READSZ	4096

static char *
try_mmap(FILE *fp, size_t *lenp)
{
	long pos;
	void *mem;

	if (fseek(fp, 0, SEEK_END) == -1) {
		if (errno != ESPIPE)
			err(EX_IOERR, "fseek");
		return NULL;
	}
	if ((pos = ftell(fp)) == -1)
		err(EX_IOERR, "ftell");

	mem = mmap(NULL, (size_t)pos, PROT_READ, MAP_SHARED,
	    fileno(fp), 0);
	if (!mem) {
		if (errno != EACCES)
			err(EX_IOERR, "mmap");
		rewind(fp);
		return NULL;
	}

	*lenp = (size_t)pos;
	return mem;
}

static char *
tmp_fallback(FILE *fp, char *oldbuf, size_t oldbuf_len, size_t *lenp)
{
	static char buf[4096];
	FILE *tempf;
	size_t nr;
	char *data;

	if (!(tempf = tmpfile()))
		err(EX_IOERR, "tmpfile");
	if (!fwrite(oldbuf, oldbuf_len, 1, tempf))
		err(EX_IOERR, "write to tmpfile");

	while ((nr = fread(buf, 1, sizeof(buf), fp)))
		if (!fwrite(buf, nr, 1, tempf))
			err(EX_IOERR, "write to tmpfile");
	if (ferror(fp))
		err(EX_IOERR, NULL);

	free(oldbuf);
	fclose(fp);

	if (!(data = try_mmap(tempf, lenp)))
		err(EX_IOERR, "fseek/mmap on tmpfile");

	return data;
}

static char *
read_all(FILE *fp, size_t *lenp)
{
	char *buf=NULL, *buf_new;
	size_t buf_len=0, buf_cap=0;
	size_t nr;

	if ((buf = try_mmap(fp, lenp)))
		return buf;

	for (;;) {
		if (buf_len + READSZ > buf_cap) {
			buf_cap = buf_cap ? buf_cap*2 : READSZ;
			buf_new = realloc(buf, buf_cap);
			if (!buf_new)
				return tmp_fallback(fp, buf, buf_len,
				    lenp);
			buf = buf_new;
		}
		if (!(nr = fread(buf+buf_len, 1, READSZ, fp)))
			break;
		buf_len += nr;
	}
	if (ferror(fp))
		err(EX_IOERR, NULL);

	*lenp = buf_len;
	return buf;
}

static char **
get_recs(char *buf, size_t buf_len, size_t *lenp)
{
	char **recs;
	size_t recs_len=0, recs_cap=64;
	size_t i;

	if (!(recs = malloc(recs_cap*sizeof(*recs))))
		err(EX_UNAVAILABLE, "malloc");
	recs[recs_len++] = buf;

	for (i=0; i<buf_len-1; i++) {
		if (buf[i] != '\n')
			continue;
		if (recs_len >= recs_cap) {
			recs_cap *= 2;
			recs = realloc(recs, recs_cap*sizeof(*recs));
			if (!recs)
				err(EX_UNAVAILABLE, "realloc");
		}
		recs[recs_len++] = &buf[i+1];
	}

	*lenp = recs_len;
	return recs;
}

static void
shuf(char **recs, size_t len)
{
	size_t i,j;
	void *tmp;

	for (i=0; i<len-1; i++) {
		j = i + random() % (len-i);
		tmp = recs[i];
		recs[i] = recs[j];
		recs[j] = tmp;
	}
}

int
main(int argc, char **argv)
{
	int c;
	FILE *fp;
	char *buf, **recs, *end;
	size_t buf_len, recs_len;
	size_t i;

	srandom(time(NULL));

	while ((c = getopt(argc, argv, "")) != -1)
		errx(EX_USAGE, "usage: shuf [file ...]");

	argc -= optind;
	argv += optind;

	if (argc > 1)
		errx(EX_USAGE, "usage: shuf [file]");
	else if (argc == 0 || !strcmp(argv[0], "-"))
		fp = stdin;
	else if (!(fp = fopen(argv[0], "r")))
		err(EX_NOINPUT, "%s", argv[0]);

	buf = read_all(fp, &buf_len);
	recs = get_recs(buf, buf_len, &recs_len);
	shuf(recs, recs_len);

	for (i=0; i<recs_len; i++) {
		end = recs[i];
		while (*end != '\n' && end < buf+buf_len)
			end++;
		fwrite(recs[i], end-recs[i], 1, stdout);
		putchar('\n');
	}

	return 0;
}
