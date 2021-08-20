#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>

#ifdef _WIN32
# include <windows.h>
# include <io.h>
# define EX_USAGE	64
# define EX_NOINPUT	66
# define EX_UNAVAILABLE	69
# define EX_OSERR	71
# define EX_IOERR	74
#else
# include <sys/mman.h>
# include <sysexits.h>
# include <err.h>
# define HAVE_ERR
# define HAVE_MMAP
# define HAVE_RANDOM
#endif

#define READSZ	((size_t)1024*1024)

static int verbosity = 0;

#ifndef HAVE_ERR
static noreturn void
err(int code, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", strerror(errno));
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);

	exit(code);
}

static noreturn void
errx(int code, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);

	exit(code);
}
#endif

static void
debugf(const char *fmt, ...)
{
	va_list ap;

	if (verbosity >= 2) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
}

#ifdef HAVE_MMAP /* not used otherwise */
static int
try_getlen(FILE *fp, size_t *lenp)
{
	int64_t pos;

	if (fseek(fp, 0, SEEK_END) == -1) {
		/* EINVAL is for win32 */
		if (errno != ESPIPE && errno != EINVAL)
			err(EX_IOERR, "fseek");
		debugf("fseek failed\n");
		return -1;
	}

#ifdef _WIN32
	if ((pos = _ftelli64(fp)) == -1)
#else
	if ((pos = ftell(fp)) == -1)
#endif
		err(EX_IOERR, "ftell");

	rewind(fp);
	*lenp = (size_t)pos;
	return 0;
}
#endif

static void *
try_readall(FILE *fp, size_t *lenp)
{
	char *buf, *buf_new;
	size_t nr, len=0, cap=READSZ;

	if (!(buf = malloc(cap))) {
		debugf("malloc failed\n");
		return NULL;
	}

	while ((nr = fread(buf+len, 1, READSZ, fp))) {
		len += nr;
		if (len+READSZ <= cap)
			continue;
		cap *= 2;
		if (!(buf_new = realloc(buf, cap))) {
			debugf("realloc failed\n");
			break;
		}
		buf = buf_new;
	}

	*lenp = len;
	return buf;
}

#ifdef HAVE_MMAP /* not used otherwise */
static void
copy_all(FILE *src, FILE *dst, size_t *lenp)
{
	static char buf[READSZ];
	size_t nr, len=0;

	while ((nr = fread(buf, 1, READSZ, src))) {
		if (!fwrite(buf, nr, 1, dst))
			err(EX_IOERR, "write to tmpfile");
		len += nr;
	}
	if (ferror(src))
		err(EX_IOERR, NULL);

	*lenp = len;
}
#endif

static char *
stubborn_mmap(FILE *fp, size_t *lenp)
{
	size_t len_read=0;
	char *mem;
#ifdef HAVE_MMAP
	size_t len, len_copied;
	FILE *tempf;

	debugf("trying mmap... ");
	if (try_getlen(fp, &len) != -1) {
		mem = mmap(NULL, len, PROT_READ, MAP_SHARED,
		    fileno(fp), 0);
		if (mem) {
			debugf("succeeded\n");
			*lenp = len;
			return mem;
		}
		if (errno != EACCES)
			err(EX_OSERR, "mmap");
	}
#else
	debugf("mmap not available\n");
#endif

	debugf("trying full read... ");
	if ((mem = try_readall(fp, &len_read)))
		if (feof(fp)) {
			debugf("succeeded\n");
			*lenp = len_read;
			return mem;
		}

#ifdef HAVE_MMAP
	debugf("using a tmpfile... ");
	if (!(tempf = tmpfile()))
		err(EX_OSERR, "tmpfile");

	/* write any data read so far */
	if (mem && !fwrite(mem, len_read, 1, tempf)) {
		err(EX_IOERR, "writing to tmpfile");
		free(mem);
	}

	copy_all(fp, tempf, &len_copied);
	fclose(fp);

	*lenp = len = len_read + len_copied;
	mem = mmap(NULL, len, PROT_READ, MAP_SHARED, fileno(tempf), 0);
	if (!mem)
		err(EX_OSERR, "mmap of tmpfile");

	debugf("succeeded\n");
	return mem;
#else
	errx(1, "out of memory");
#endif
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
#ifdef HAVE_RANDOM
		j = i + random() % (len-i);
#else
		j = i + rand() % (len-i);
#endif
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

#ifdef HAVE_RANDOM
	srandom(time(NULL));
#else
	srand(time(NULL));
#endif

	while ((c = getopt(argc, argv, "v")) != -1)
		switch (c) {
		case 'v': verbosity++; break;
		default:
			errx(EX_USAGE, "usage: shuf [file ...]");
		}

	argc -= optind;
	argv += optind;

	if (argc > 1)
		errx(EX_USAGE, "usage: shuf [file]");
	else if (argc == 0 || !strcmp(argv[0], "-"))
		fp = stdin;
	else if (!(fp = fopen(argv[0], "r")))
		err(EX_NOINPUT, "%s", argv[0]);

	buf = stubborn_mmap(fp, &buf_len);

	debugf("locating lines\n");
	recs = get_recs(buf, buf_len, &recs_len);

	debugf("shuffling\n");
	shuf(recs, recs_len);

	debugf("printing\n");
	for (i=0; i<recs_len; i++) {
		end = recs[i];
		while (*end != '\n' && end < buf+buf_len)
			end++;
		fwrite(recs[i], end-recs[i], 1, stdout);
		putchar('\n');
	}

	return 0;
}
