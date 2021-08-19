#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <sysexits.h>
#include <err.h>

#define READSZ	4096

int
main(int argc, char **argv)
{
	int c;
	FILE *fp;
	char *buf=NULL, **recs=NULL, *tmp, *end;
	size_t buf_len=0, buf_cap=0;
	size_t recs_len=0, recs_cap=0;
	size_t nr, i,j;

	srandom(time(NULL));

	while ((c = getopt(argc, argv, "")) != -1)
		errx(EX_USAGE, "usage: shuf [file ...]");

	argc -= optind;
	argv += optind;

	if (argc > 1)
		errx(EX_USAGE, "usage: shuf [file ...]");
	else if (argv == 0 || !strcmp(argv[0], "-"))
		fp = stdin;
	else if (!(fp = fopen(argv[0], "r")))
		err(EX_NOINPUT, "%s", argv[0]);

	for (;;) {
		if (buf_len + READSZ > buf_cap) {
			buf_cap = buf_cap ? buf_cap*2 : READSZ;
			if (!(buf = realloc(buf, buf_cap)))
				err(EX_UNAVAILABLE, "realloc");
		}
		if (!(nr = fread(buf+buf_len, 1, READSZ, fp)))
			break;
		buf_len += nr;
	}
	if (ferror(fp))
		err(EX_IOERR, NULL);

	for (i=0; i<buf_len-1; i++) {
		if (buf[i] != '\n')
			continue;
		if (recs_len >= recs_cap) {
			recs_cap = recs_cap ? recs_cap*2 : 64;
			recs = realloc(recs, recs_cap*sizeof(*recs));
			if (!recs)
				err(EX_UNAVAILABLE, "realloc");
		}
		recs[recs_len++] = &buf[i+1];
	}

	for (i=0; i<recs_len-1; i++) {
		j = i + random() % (recs_len-i);
		tmp = recs[i];
		recs[i] = recs[j];
		recs[j] = tmp;
	}

	for (i=0; i<recs_len; i++) {
		end = recs[i];
		while (*end != '\n' && end < buf+buf_len)
			end++;
		fwrite(recs[i], end-recs[i], 1, stdout);
		putchar('\n');
	}

	return 0;
}
