/*** dukasdec.c -- dukascopy decoder
 *
 * Copyright (C) 2009-2015 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of dukascli.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***/
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#if defined HAVE_SYS_TYPES_H
/* for ssize_t */
# include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include "boobs.h"
#include "nifty.h"

typedef union {
	double d;
	int64_t i;
} float64_t;

typedef union {
	float d;
	int32_t i;
} float32_t;

struct dc_s {
	uint64_t ts;
	float64_t ap;
	float64_t bp;
	float64_t aq;
	float64_t bq;
};

struct dcc_s {
	uint64_t ts;
	float64_t o;
	float64_t c;
	float64_t l;
	float64_t h;
	float64_t v;
};

struct dqbi5_s {
	uint32_t ts;
	uint32_t ap;
	uint32_t bp;
	float32_t aq;
	float32_t bq;
};

struct dcbi5_s {
	uint32_t ts;
	uint32_t o;
	uint32_t c;
	uint32_t l;
	uint32_t h;
	float32_t v;
};

struct ctx_s {
	/** symbol in question */
	const char *sym;
	/** offset for timestamps relative to something other than epoch */
	int32_t tsoff;

	unsigned int all_ticks_p:1U;
};


static __attribute__((format(printf, 1, 2))) void
serror(const char *fmt, ...)
{
	va_list vap;
	va_start(vap, fmt);
	vfprintf(stderr, fmt, vap);
	va_end(vap);
	if (errno) {
		fputc(':', stderr);
		fputc(' ', stderr);
		fputs(strerror(errno), stderr);
	}
	fputc('\n', stderr);
	return;
}


static int
rd1(struct dc_s *restrict b, int fd)
{
	if (UNLIKELY(read(fd, b, sizeof(*b)) <= 0)) {
		return -1;
	}
	b->ts = be64toh(b->ts);
	b->ap.i = be64toh(b->ap.i);
	b->bp.i = be64toh(b->bp.i);
	b->aq.i = be64toh(b->aq.i);
	b->bq.i = be64toh(b->bq.i);
	return 0;
}

static size_t
rd1bi5(struct dqbi5_s *restrict b, size_t nb, int fd)
{
	ssize_t nrd;

	if (UNLIKELY((nrd = read(fd, b, nb * sizeof(*b))) <= 0)) {
		return 0U;
	}
	do {
		b->ts = be32toh(b->ts);
		b->ap = be32toh(b->ap);
		b->bp = be32toh(b->bp);
		b->aq.i = be32toh(b->aq.i);
		b->bq.i = be32toh(b->bq.i);
	} while (b++, --nb);
	return nrd / sizeof(*b);
}

static void
dump_tick_bi5(const struct ctx_s ctx[static 1U], struct dqbi5_s *tl)
{
/* create one or more sparse ticks, sl1t_t objects */
	static struct dqbi5_s last;
	unsigned int ts = tl->ts / 1000;
	unsigned int ms = tl->ts % 1000;
	int32_t off = ctx->tsoff;

	if (ctx->all_ticks_p ||
	    tl->bp != last.bp || tl->bq.i != last.bq.i) {
		printf("%u.%03u\t%s\tb\t%u\t%f\n",
		       ts + off, ms, ctx->sym, tl->bp, tl->bq.d);
	}
	if (ctx->all_ticks_p ||
	    tl->ap != last.ap || tl->aq.i != last.aq.i) {
		printf("%u.%03u\t%s\ta\t%u\t%f\n",
		       ts + off, ms, ctx->sym, tl->ap, tl->aq.d);
	}
	/* for our compressor */
	if (LIKELY(!ctx->all_ticks_p)) {
		last = *tl;
	}
	return;
}

static int
dump(const struct ctx_s ctx[static 1U], int fd)
{
	union {
		struct dc_s bin[1U];
		struct dqbi5_s bi5[2U];
	} buf = {};
	size_t i = 0U;

	/* run the probe */
	if (rd1bi5(buf.bi5, countof(buf.bi5), fd) < 2U) {
		/* huh? */
		return -1;
	} else {
		/* the only thing we can make assumptions
		 * about is the timestamp
		 * we check the two stamps in bi5
		 * and compare their distance */
		uint32_t ts0 = buf.bi5[0U].ts;
		uint32_t ts1 = buf.bi5[1U].ts;

		if (ts1 - ts0 > 60/*min*/ * 60/*sec*/ * 1000/*msec*/) {
			/* definitely old_fmt */
			return -1;
		}
		goto dump_bi5;
	}

again:
	/* main loop */
	switch (i = 0U, rd1bi5(buf.bi5, countof(buf.bi5), fd)) {
	dump_bi5:
	case 2U:
		dump_tick_bi5(ctx, buf.bi5 + i++);
	case 1U:
		dump_tick_bi5(ctx, buf.bi5 + i++);
		goto again;
	case 0U:
		break;
	}
	return 0;
}

static int
guess(struct ctx_s *restrict ctx, const char *fn)
{
/* guess the specs from the filename FN. */
	/* currency abbrev stop-set */
	static char ccy_ss[] = "ABCDEFGHJKNOPRSUXYZ";
	static char dt_ss[] = "0123456789/";
	const char *x, *y;

	/* try to snarf off the ccys first */
	x = fn;
	while ((x = strpbrk(x, ccy_ss)) != NULL) {
		if (strspn(x, ccy_ss) == 6UL) {
			static char sym[8U];
			memcpy(sym, x, 6U);
			ctx->sym = sym;
			break;
		}
		x++;
	}
	/* date and time should come afterwards */
	while ((x = strpbrk(x, dt_ss)) != NULL) {
		struct tm tm[1];

		if (strspn(x++, dt_ss) >= 1 + 4 + 1 + 2 + 1 + 2 + 1 &&
		    (y = strptime(x, "%Y/%m/%d", tm)) != NULL &&
		    *y == '/') {
			/* found something like /YYYY/mm/dd */
			/* zap to the hour bit */
			while (*++y == '/');
			if (y[2U] != 'h') {
				/* not in the form /..h */
				continue;
			}
			/* complete the hour bit */
			tm->tm_hour = atoi(y);
			tm->tm_min = 0;
			tm->tm_sec = 0;
			ctx->tsoff = timegm(tm);
			break;
		}
	}
	return 0;
}


#include "dukasdec.yucc"

int
main(int argc, char *argv[])
{
	static yuck_t argi[1U];
	static struct ctx_s ctx[1U];
	int rc = 0;

	if (yuck_parse(argi, argc, argv) < 0) {
		rc = 1;
		goto out;
	} else if (!argi->nargs) {
		errno = 0, serror("\
Error: TICK_FILE is mandatory");
		rc = 1;
		goto out;
	}

	for (size_t i = 0U; i < argi->nargs; i++) {
		const char *fn = argi->args[i];
		int fd;

		if ((fd = open(fn, O_RDONLY)) < 0) {
			serror("\
Error: cannot open file `%s'", fn);
			continue;
		} else if (guess(ctx, fn) < 0) {
			errno = 0, serror("\
Error: cannot guess symbol and time parameters from filename");
		} else if (dump(ctx, fd) < 0) {
			errno = 0, serror("\
Error: file `%s' corrupted", fn);
		}
		/* clean up after ourself */
		close(fd);
	}

out:
	yuck_free(argi);
	return rc;
}

/* dukasdec.c ends here */
