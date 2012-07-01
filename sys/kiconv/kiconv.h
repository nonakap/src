/*	$NetBSD$	*/

/*-
 * Copyright (C) 2011, 2012 NONAKA Kimihiro <nonaka@netbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_KICONV_KICONV_H_
#define	_KICONV_KICONV_H_

#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/kiconv.h>

#ifdef KICONV_DEBUG
extern int kiconv_debug;
#define	KICONV_DBG(s)		printf s
#define	KICONV_DBGN(n,s)	if (kiconv_debug >= (n)) printf s
#else
#define	KICONV_DBG(s)		do {} while (/*CONSTCOND*/0)
#define	KICONV_DBGN(n,s)	do {} while (/*CONSTCOND*/0)
#endif

/* kiconv interface */
struct kiconv_ops {
	kiconv_t	(*iconv_open)(const char *, const char *, int *);
	int		(*iconv_close)(kiconv_t, int *);
	size_t		(*iconv)(kiconv_t, const char **, size_t *, char **,
			    size_t *, uint32_t, size_t *, int *);
	int		(*iconv_get_list)(char ***, size_t *, int *);
	void		(*iconv_free_list)(char **, size_t);

	klocale_t	(*newlocale)(const char *, int *);
	int		(*freelocale)(klocale_t, int *);

	int		(*mbsinit)(const mbstate_t *, klocale_t, int *);
	size_t		(*mbrlen)(const char * __restrict, size_t,
			    mbstate_t * __restrict, klocale_t, int *);
	size_t		(*mbrtowc)(wchar_t * __restrict,
			    const char * __restrict, size_t, mbstate_t *,
			    klocale_t, int *);
	size_t		(*mbsrtowcs)(wchar_t * __restrict,
			    const char ** __restrict, size_t,
			    mbstate_t * __restrict, klocale_t, int *);
	size_t		(*wcrtomb)(char * __restrict, wchar_t wc,
			    mbstate_t * __restrict, klocale_t, int *);
	size_t		(*wcsrtombs)(char * __restrict,
			    const wchar_t ** __restrict, size_t,
			    mbstate_t * __restrict, klocale_t, int *);
	wint_t		(*btowc)(int, klocale_t, int *);
	int		(*wctob)(wint_t, klocale_t, int *);
	int		(*wcwidth)(wchar_t, klocale_t);
	int		(*wcswidth)(const wchar_t *, size_t, klocale_t);

	int		(*iswalnum)(wint_t, klocale_t);
	int		(*iswalpha)(wint_t, klocale_t);
	int		(*iswblank)(wint_t, klocale_t);
	int		(*iswcntrl)(wint_t, klocale_t);
	int		(*iswdigit)(wint_t, klocale_t);
	int		(*iswgraph)(wint_t, klocale_t);
	int		(*iswlower)(wint_t, klocale_t);
	int		(*iswprint)(wint_t, klocale_t);
	int		(*iswpunct)(wint_t, klocale_t);
	int		(*iswspace)(wint_t, klocale_t);
	int		(*iswupper)(wint_t, klocale_t);
	int		(*iswxdigit)(wint_t, klocale_t);
	wint_t		(*towupper)(wint_t, klocale_t);
	wint_t		(*towlower)(wint_t, klocale_t);
	wctype_t	(*wctype)(const char *, klocale_t);
	wctrans_t	(*wctrans)(const char *, klocale_t);
	int		(*iswctype)(wint_t, wctype_t, klocale_t);
	wint_t		(*towctrans)(wint_t, wctrans_t, klocale_t);
};

void kiconv_attach_ops(struct kiconv_ops *);
void kiconv_detach_ops(struct kiconv_ops *);

/* encoding/iconv/mapper module interface */
struct kiconv_getops {
	char kg_name[32];
	void *kg_getops;
	TAILQ_ENTRY(kiconv_getops) kg_list;
};

void kiconv_attach_getops(struct kiconv_getops *);
void kiconv_detach_getops(struct kiconv_getops *);
struct kiconv_getops *kiconv_find_getops(const char *);

#define	KICONV_GETOPS(name)					\
static struct kiconv_getops kiconv_##name##_getops = {		\
	.kg_name = #name,					\
	.kg_getops = _citrus_##name##_getops,			\
};

/* kiconv's malloc type */
MALLOC_DECLARE(M_KICONV);

/* LINE_MAX pool and cache */
extern pool_cache_t kiconv_linebuf_cache;
#define KICONV_LINEBUF_GET()     pool_cache_get(kiconv_linebuf_cache, PR_WAITOK)
#define KICONV_LINEBUF_PUT(pnb)  pool_cache_put(kiconv_linebuf_cache, (pnb))

/* from paths.h */
#define	_PATH_CSMAPPER	"/usr/share/i18n/csmapper"
#define	_PATH_ESDB	"/usr/share/i18n/esdb"
#define	_PATH_ICONV	"/usr/share/i18n/iconv"
#define	_PATH_LOCALE	"/usr/share/locale"

/* XXX */
static __inline char *
strdup(const char *src)
{
	size_t sz;
	char *dst;

	KASSERT(src != NULL);

	sz = strlen(src) + 1;
	dst = malloc(sz, M_KICONV, M_WAITOK);
	strlcpy(dst, src, sz);

	return dst;
}

extern size_t __mb_cur_max;
extern size_t __mb_len_max_runtime;

#define	_DEFAULT_LOCALE 	"en_US.UTF-8"
#define	_DEFAULT_ENCODING	"UTF-8"

#ifdef _KICONV_LOCAL
kiconv_t	_kiconv_open(const char *, const char *, int *);
int		_kiconv_close(kiconv_t, int *);
size_t		_kiconv_conv(kiconv_t, const char **, size_t *, char **,
		    size_t *, uint32_t, size_t *, int *);

klocale_t	_kiconv_newlocale(const char *, int *);
int		_kiconv_freelocale(klocale_t, int *);

int		_kiconv_mbsinit(const mbstate_t *, klocale_t, int *);
size_t		_kiconv_mbrlen(const char * __restrict, size_t,
		    mbstate_t * __restrict, klocale_t, int *);
size_t		_kiconv_mbrtowc(wchar_t * __restrict,
		    const char * __restrict, size_t, mbstate_t *,
		    klocale_t, int *);
size_t		_kiconv_mbsrtowcs(wchar_t * __restrict,
		    const char ** __restrict, size_t,
		    mbstate_t * __restrict, klocale_t, int *);
size_t		_kiconv_wcrtomb(char * __restrict, wchar_t wc,
		    mbstate_t * __restrict, klocale_t, int *);
size_t		_kiconv_wcsrtombs(char * __restrict,
		    const wchar_t ** __restrict, size_t,
		    mbstate_t * __restrict, klocale_t, int *);
wint_t		_kiconv_btowc(int, klocale_t, int *);
int		_kiconv_wctob(wint_t, klocale_t, int *);
int		_kiconv_wcwidth(wchar_t, klocale_t);
int		_kiconv_wcswidth(const wchar_t *, size_t, klocale_t);

int		_kiconv_iswalnum(wint_t, klocale_t);
int		_kiconv_iswalpha(wint_t, klocale_t);
int		_kiconv_iswblank(wint_t, klocale_t);
int		_kiconv_iswcntrl(wint_t, klocale_t);
int		_kiconv_iswdigit(wint_t, klocale_t);
int		_kiconv_iswgraph(wint_t, klocale_t);
int		_kiconv_iswlower(wint_t, klocale_t);
int		_kiconv_iswprint(wint_t, klocale_t);
int		_kiconv_iswpunct(wint_t, klocale_t);
int		_kiconv_iswspace(wint_t, klocale_t);
int		_kiconv_iswupper(wint_t, klocale_t);
int		_kiconv_iswxdigit(wint_t, klocale_t);
wint_t		_kiconv_towupper(wint_t, klocale_t);
wint_t		_kiconv_towlower(wint_t, klocale_t);
wctype_t	_kiconv_wctype(const char *, klocale_t);
wctrans_t	_kiconv_wctrans(const char *, klocale_t);
int		_kiconv_iswctype(wint_t, wctype_t, klocale_t);
wint_t		_kiconv_towctrans(wint_t, wctrans_t, klocale_t);
#endif

#endif	/* _KICONV_KICONV_H_ */
