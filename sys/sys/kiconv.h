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

#ifndef	_SYS_KICONV_H_
#define	_SYS_KICONV_H_

#if defined(_BSD_WCHAR_T_) && !defined(__cplusplus)
typedef _BSD_WCHAR_T_   wchar_t;
#undef  _BSD_WCHAR_T_
#endif

#ifdef _BSD_MBSTATE_T_
typedef _BSD_MBSTATE_T_ mbstate_t;
#undef _BSD_MBSTATE_T_
#endif

#ifdef  _BSD_WINT_T_
typedef _BSD_WINT_T_    wint_t;
#undef  _BSD_WINT_T_
#endif

#ifdef  _BSD_WCTRANS_T_
typedef _BSD_WCTRANS_T_ wctrans_t;
#undef  _BSD_WCTRANS_T_
#endif

#ifdef  _BSD_WCTYPE_T_
typedef _BSD_WCTYPE_T_  wctype_t;
#undef  _BSD_WCTYPE_T_
#endif

#ifndef WEOF
#define WEOF    ((wint_t)-1)
#endif

#ifndef EOF
#define EOF    (-1)
#endif

#define	MB_LEN_MAX	32	/* Allow ISO/IEC 2022 */

/* wchar_t PATH_MAX pool and cache */
extern pool_cache_t kiconv_wchar_pathbuf_cache;
#define WPNBUF_GET() \
	pool_cache_get(kiconv_wchar_pathbuf_cache, PR_WAITOK)
#define WPNBUF_PUT(wpnb) \
	pool_cache_put(kiconv_wchar_pathbuf_cache, (wpnb))

typedef struct _kiconv_impl_t	*kiconv_t;
typedef struct _klocale_impl_t	*klocale_t;

void		kiconv_init(void);

kiconv_t	kiconv_open(const char *, const char *, int *);
int		kiconv_close(kiconv_t, int *);
size_t		kiconv_conv(kiconv_t, const char **, size_t *, char **,
		    size_t *, int *);
size_t		__kiconv_conv(kiconv_t, const char **, size_t *, char **,
		    size_t *, uint32_t, size_t *, int *);

klocale_t	kiconv_newlocale(const char *, int *);
int		kiconv_freelocale(klocale_t, int *);

int		kiconv_mbsinit_l(const mbstate_t *, klocale_t, int *);
size_t		kiconv_mbrlen_l(const char * __restrict, size_t,
		    mbstate_t * __restrict, klocale_t, int *);
size_t		kiconv_mbrtowc_l(wchar_t * __restrict, const char * __restrict,
		    size_t, mbstate_t *, klocale_t, int *);
size_t		kiconv_mbsrtowcs_l(wchar_t * __restrict,
		    const char ** __restrict, size_t, mbstate_t * __restrict,
		    klocale_t, int *);
size_t		kiconv_wcrtomb_l(char * __restrict, wchar_t wc,
		    mbstate_t * __restrict, klocale_t, int *);
size_t		kiconv_wcsrtombs_l(char * __restrict,
		    const wchar_t ** __restrict, size_t,
		    mbstate_t * __restrict, klocale_t, int *);
wint_t		kiconv_btowc_l(int, klocale_t, int *);
int		kiconv_wctob_l(wint_t, klocale_t, int *);
int		kiconv_wcwidth_l(wchar_t, klocale_t);
int		kiconv_wcswidth_l(const wchar_t *, size_t, klocale_t);

int		kiconv_iswalnum_l(wint_t, klocale_t);
int		kiconv_iswalpha_l(wint_t, klocale_t);
int		kiconv_iswblank_l(wint_t, klocale_t);
int		kiconv_iswcntrl_l(wint_t, klocale_t);
int		kiconv_iswdigit_l(wint_t, klocale_t);
int		kiconv_iswgraph_l(wint_t, klocale_t);
int		kiconv_iswlower_l(wint_t, klocale_t);
int		kiconv_iswprint_l(wint_t, klocale_t);
int		kiconv_iswpunct_l(wint_t, klocale_t);
int		kiconv_iswspace_l(wint_t, klocale_t);
int		kiconv_iswupper_l(wint_t, klocale_t);
int		kiconv_iswxdigit_l(wint_t, klocale_t);
wint_t		kiconv_towupper_l(wint_t, klocale_t);
wint_t		kiconv_towlower_l(wint_t, klocale_t);
wctype_t	kiconv_wctype_l(const char *, klocale_t);
wctrans_t	kiconv_wctrans_l(const char *, klocale_t);
int		kiconv_iswctype_l(wint_t, wctype_t, klocale_t);
wint_t		kiconv_towctrans_l(wint_t, wctrans_t, klocale_t);

#endif	/* _SYS_KICONV_H_ */
