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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/pool.h>
#include <sys/module.h>

#define	_KICONV_LOCAL
#include <kiconv/kiconv.h>

#include "citrus_types.h"
#include "citrus_module.h"
#include "citrus_esdb.h"
#include "citrus_hash.h"
#include "citrus_iconv.h"
#include "citrus_ctype.h"

#include "setlocale_local.h"

#include "runetype_local.h"
#include "runetype_file.h"
#include "multibyte.h"
#include "rune.h"
#include "_wctype_local.h"
#include "_wctrans_local.h"


/*----------------------------------------------------------------------------
 * iconv
 */
#define	ISBADF(_h_)	(!(_h_) || (_h_) == (kiconv_t)-1)

kiconv_t
_kiconv_open(const char *out, const char *in, int *errnop)
{
	struct _citrus_iconv *handle;
	int error;

	if (in == NULL)
		in = _DEFAULT_ENCODING;
	if (out == NULL)
		out = _DEFAULT_ENCODING;

	error = _citrus_iconv_open(&handle, _PATH_ICONV, in, out);
	if (error) {
		if (errnop)
			*errnop = (error == ENOENT) ? EINVAL : error;
		return (kiconv_t)-1;
	}
	return (kiconv_t)(void *)handle;
}

int
_kiconv_close(kiconv_t handle, int *errnop)
{

	if (ISBADF(handle)) {
		if (errnop)
			*errnop = EBADF;
		return -1;
	}

	_citrus_iconv_close((struct _citrus_iconv *)(void *)handle);
	return 0;
}

size_t
_kiconv_conv(kiconv_t handle, const char **in, size_t *szin, char **out,
    size_t *szout, uint32_t flags, size_t *invalids, int *errnop)
{
	int error;
	size_t ret;

	if (ISBADF(handle)) {
		if (errnop)
			*errnop = EBADF;
		return (size_t)-1;
	}

	error = _citrus_iconv_convert((struct _citrus_iconv *)(void *)handle,
	    in, szin, out, szout, flags, &ret);
	if (invalids)
		*invalids = ret;
	if (error) {
		if (errnop)
			*errnop = error;
		ret = (size_t)-1;
	}

	return ret;
}

/*----------------------------------------------------------------------------
 * locale
 */
klocale_t
_kiconv_newlocale(const char *locale, int *errnop)
{
	extern const _klocale_category_t _citrus_LC_CTYPE_desc;
	const _klocale_category_t *l = &_citrus_LC_CTYPE_desc;
	struct _klocale_impl_t *dst;

	if (locale == NULL)
		locale = _DEFAULT_LOCALE;

	dst = malloc(sizeof(*dst), M_KICONV, M_WAITOK);
	*dst = *_locale_to_impl(NULL);
	(*l->setlocale)(locale, dst);

	if (errnop)
		*errnop = 0;
	return (klocale_t)dst;
}

int
_kiconv_freelocale(klocale_t locale, int *errnop)
{
	struct _klocale_impl_t *impl;

	if (locale == NULL) {
		if (errnop)
			*errnop = EINVAL;
		return -1;
	}
	impl = (struct _klocale_impl_t *)locale;
	if (impl == *_current_locale())
		*_current_locale() = &_global_locale;
	free(impl, M_KICONV);
	return 0;
}

/*----------------------------------------------------------------------------
 * wchar
 */
#define _RUNE_LOCALE(locale) \
    ((_RuneLocale *)(_locale_to_impl(locale))->part_impl[(size_t)LC_CTYPE])

#define _CITRUS_CTYPE(locale) \
    (_RUNE_LOCALE(locale)->rl_citrus_ctype)

size_t
_kiconv_mbrlen(const char *s, size_t n, mbstate_t *ps, klocale_t locale,
    int *errnop)
{
	size_t ret;
	int err0;

	_fixup_ps(_RUNE_LOCALE(locale), ps, s == NULL);

	err0 = _citrus_ctype_mbrlen(_ps_to_ctype(ps), s, n,
				     _ps_to_private(ps), &ret);
	if (err0 && errnop)
		*errnop = err0;

	return ret;
}

int
_kiconv_mbsinit(const mbstate_t *ps, klocale_t locale, int *errnop)
{
	int ret;
	int err0;
	_RuneLocale *rl;

	if (ps == NULL)
		return 1;

	if (_ps_to_runelocale(ps) == NULL)
		rl = _RUNE_LOCALE(locale);
	else
		rl = _ps_to_runelocale(ps);

	/* mbsinit should cause no error... */
	err0 = _citrus_ctype_mbsinit(rl->rl_citrus_ctype,
				      _ps_to_private_const(ps), &ret);
	if (err0 && errnop)
		*errnop = err0;

	return ret;
}

size_t
_kiconv_mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps,
    klocale_t locale, int *errnop)
{
	size_t ret;
	int err0;

	_fixup_ps(_RUNE_LOCALE(locale), ps, s == NULL);

	err0 = _citrus_ctype_mbrtowc(_ps_to_ctype(ps), pwc, s, n,
				       _ps_to_private(ps), &ret);
	if (err0 && errnop)
		*errnop = err0;

	return ret;
}

size_t
_kiconv_mbsrtowcs(wchar_t *pwcs, const char **s, size_t n, mbstate_t *ps,
    klocale_t locale, int *errnop)
{
	size_t ret;
	int err0;

	_fixup_ps(_RUNE_LOCALE(locale), ps, s == NULL);

	err0 = _citrus_ctype_mbsrtowcs(_ps_to_ctype(ps), pwcs, s, n,
					_ps_to_private(ps), &ret);
	if (err0 && errnop)
		*errnop = err0;

	return ret;
}

size_t
_kiconv_wcrtomb(char *s, wchar_t wc, mbstate_t *ps, klocale_t locale,
    int *errnop)
{
	size_t ret;
	int err0;

	_fixup_ps(_RUNE_LOCALE(locale), ps, s == NULL);

	err0 = _citrus_ctype_wcrtomb(_ps_to_ctype(ps), s, wc,
				       _ps_to_private(ps), &ret);
	if (err0 && errnop)
		*errnop = err0;

	return ret;
}

size_t
_kiconv_wcsrtombs(char *s, const wchar_t **ppwcs, size_t n, mbstate_t *ps,
    klocale_t locale, int *errnop)
{
	size_t ret;
	int err0;

	_fixup_ps(_RUNE_LOCALE(locale), ps, s == NULL);

	err0 = _citrus_ctype_wcsrtombs(_ps_to_ctype(ps), s, ppwcs, n,
					_ps_to_private(ps), &ret);
	if (err0 && errnop)
		*errnop = err0;

	return ret;
}

wint_t
_kiconv_btowc(int c, klocale_t locale, int *errnop)
{
	wint_t ret;
	int err0;

	err0 = _citrus_ctype_btowc(_CITRUS_CTYPE(locale), c, &ret);
	if (err0 && errnop)
		*errnop = err0;

	return ret;
}

int
_kiconv_wctob(wint_t wc, klocale_t locale, int *errnop)
{
	int ret;
	int err0;

	err0 = _citrus_ctype_wctob(_CITRUS_CTYPE(locale), wc, &ret);
	if (err0 && errnop)
		*errnop = err0;

	return ret;
}

int
_kiconv_wcwidth(wchar_t wc, klocale_t locale)
{
	_RuneLocale const *rl;
	_RuneType x;

	_DIAGASSERT(locale != NULL);

	if (wc == L'\0')
		return 0;
	rl = _RUNE_LOCALE(locale);
	x = _runetype_priv(rl, wc);
	if (x & _RUNETYPE_R)
		return ((u_int)x & _RUNETYPE_SWM) >> _RUNETYPE_SWS;
	return -1;
}

int
_kiconv_wcswidth(const wchar_t *wcs, size_t wn, klocale_t locale)
{
	_RuneLocale const *rl;
	_RuneType x;
	int width;

	_DIAGASSERT(wcs != NULL);
	_DIAGASSERT(locale != NULL);

	rl = _RUNE_LOCALE(locale);
	width = 0;
	for (width = 0; wn > 0 && *wcs != L'\0'; wcs++, --wn) {
		x = _runetype_priv(rl, *wcs);
		if ((x & _RUNETYPE_R) == 0)
			return -1;
		width += ((u_int)x & _RUNETYPE_SWM) >> _RUNETYPE_SWS;
	}
	return width;
}

/*----------------------------------------------------------------------------
 * wctype
 */
#define _RUNE_ISWCTYPE_FUNC(name, index)		\
int							\
_kiconv_isw##name(wint_t wc, klocale_t locale)		\
{							\
	_RuneLocale const *rl;				\
	_WCTypeEntry const *te;				\
							\
	rl = _RUNE_LOCALE(locale);			\
	te = &rl->rl_wctype[index];			\
	return _iswctype_priv(rl, wc, te);		\
}
_RUNE_ISWCTYPE_FUNC(alnum,  _WCTYPE_INDEX_ALNUM)
_RUNE_ISWCTYPE_FUNC(alpha,  _WCTYPE_INDEX_ALPHA)
_RUNE_ISWCTYPE_FUNC(blank,  _WCTYPE_INDEX_BLANK)
_RUNE_ISWCTYPE_FUNC(cntrl,  _WCTYPE_INDEX_CNTRL)
_RUNE_ISWCTYPE_FUNC(digit,  _WCTYPE_INDEX_DIGIT)
_RUNE_ISWCTYPE_FUNC(graph,  _WCTYPE_INDEX_GRAPH)
_RUNE_ISWCTYPE_FUNC(lower,  _WCTYPE_INDEX_LOWER)
_RUNE_ISWCTYPE_FUNC(print,  _WCTYPE_INDEX_PRINT)
_RUNE_ISWCTYPE_FUNC(punct,  _WCTYPE_INDEX_PUNCT)
_RUNE_ISWCTYPE_FUNC(space,  _WCTYPE_INDEX_SPACE)
_RUNE_ISWCTYPE_FUNC(upper,  _WCTYPE_INDEX_UPPER)
_RUNE_ISWCTYPE_FUNC(xdigit, _WCTYPE_INDEX_XDIGIT)

#define _RUNE_TOWCTRANS_FUNC(name, index)		\
wint_t							\
_kiconv_tow##name(wint_t wc, klocale_t locale)		\
{							\
	_RuneLocale const *rl;				\
	_WCTransEntry const *te;			\
							\
	rl = _RUNE_LOCALE(locale);			\
	te = &rl->rl_wctrans[index];			\
	return _towctrans_priv(rl, wc, te);		\
}
_RUNE_TOWCTRANS_FUNC(upper, _WCTRANS_INDEX_UPPER)
_RUNE_TOWCTRANS_FUNC(lower, _WCTRANS_INDEX_LOWER)

wctype_t
_kiconv_wctype(const char *charclass, klocale_t locale)
{
	_RuneLocale const *rl;
	size_t i;

	rl = _RUNE_LOCALE(locale);
	for (i = 0; i < _WCTYPE_NINDEXES; ++i) {
		if (!strcmp(rl->rl_wctype[i].te_name, charclass))
			return (wctype_t)(intptr_t)&rl->rl_wctype[i];
	}
	return (wctype_t)NULL;
}

wctrans_t
_kiconv_wctrans(const char *charmap, klocale_t locale)
{
	_RuneLocale const *rl;
	size_t i;

	rl = _RUNE_LOCALE(locale);
	if (rl->rl_wctrans[_WCTRANS_INDEX_LOWER].te_name == NULL)
		_wctrans_init(__UNCONST(rl));
	for (i = 0; i < _WCTRANS_NINDEXES; ++i) {
		if (!strcmp(rl->rl_wctrans[i].te_name, charmap))
			return (wctrans_t)(intptr_t)&rl->rl_wctype[i];
	}
	return (wctrans_t)NULL;
}

int
_kiconv_iswctype(wint_t wc, wctype_t charclass, klocale_t locale)
{
	_RuneLocale const *rl;
	_WCTypeEntry const *te;

	_DIAGASSERT(locale != NULL);

	if (charclass == NULL)
		return 0;

	rl = _RUNE_LOCALE(locale);
	te = (_WCTypeEntry const *)charclass;
	return _iswctype_priv(rl, wc, te);
}

wint_t
_kiconv_towctrans(wint_t wc, wctrans_t charmap, klocale_t locale)
{
	_RuneLocale const *rl;
	_WCTransEntry const *te;

	_DIAGASSERT(locale != NULL);

	if (charmap == NULL)
		return wc;

	rl = _RUNE_LOCALE(locale);
	te = (_WCTransEntry const *)charmap;
	return _towctrans_priv(rl, wc, te);
}

/*----------------------------------------------------------------------------
 * kiconv ops
 */
static struct kiconv_ops kiconv_ops = {
	.iconv_open = _kiconv_open,
	.iconv_close = _kiconv_close,
	.iconv = _kiconv_conv,

	.newlocale = _kiconv_newlocale,
	.freelocale = _kiconv_freelocale,

	.mbrlen = _kiconv_mbrlen,
	.mbsinit = _kiconv_mbsinit,
	.mbrtowc = _kiconv_mbrtowc,
	.mbsrtowcs = _kiconv_mbsrtowcs,
	.wcrtomb = _kiconv_wcrtomb,
	.wcsrtombs = _kiconv_wcsrtombs,
	.btowc = _kiconv_btowc,
	.wctob = _kiconv_wctob,
	.wcwidth = _kiconv_wcwidth,
	.wcswidth = _kiconv_wcswidth,

	.iswalnum = _kiconv_iswalnum,
	.iswalpha = _kiconv_iswalpha,
	.iswblank = _kiconv_iswblank,
	.iswcntrl = _kiconv_iswcntrl,
	.iswdigit = _kiconv_iswdigit,
	.iswgraph = _kiconv_iswgraph,
	.iswlower = _kiconv_iswlower,
	.iswprint = _kiconv_iswprint,
	.iswpunct = _kiconv_iswpunct,
	.iswspace = _kiconv_iswspace,
	.iswupper = _kiconv_iswupper,
	.iswxdigit = _kiconv_iswxdigit,
	.towupper = _kiconv_towupper,
	.towlower = _kiconv_towlower,
	.wctype = _kiconv_wctype,
	.wctrans = _kiconv_wctrans,
	.iswctype = _kiconv_iswctype,
	.towctrans = _kiconv_towctrans,
};

/*-----------------------------------------------------------------------------
 * module
 */
#include <sys/rwlock.h>
#include <sys/mutex.h>
#include <sys/module.h>

extern krwlock_t kiconv_csmapper_lock;
extern krwlock_t kiconv_mapper_lock;
extern krwlock_t kiconv_iconv_lock;
extern kmutex_t _citrus_LC_CTYPE_mutex;

MODULE(MODULE_CLASS_MISC, kiconv, NULL);

static int
kiconv_modcmd(modcmd_t cmd, void *arg)
{

	switch (cmd) {
	case MODULE_CMD_INIT:
		rw_init(&kiconv_csmapper_lock);
		rw_init(&kiconv_mapper_lock);
		rw_init(&kiconv_iconv_lock);
		mutex_init(&_citrus_LC_CTYPE_mutex, MUTEX_DEFAULT, IPL_NONE);
		kiconv_attach_ops(&kiconv_ops);
		return 0;

	case MODULE_CMD_FINI:
		kiconv_detach_ops(&kiconv_ops);
		/* XXX:XXXNONAKA MEMORY LEAK!!! must purge cache!!! */
		mutex_destroy(&_citrus_LC_CTYPE_mutex);
		rw_destroy(&kiconv_csmapper_lock);
		rw_destroy(&kiconv_mapper_lock);
		rw_destroy(&kiconv_iconv_lock);
		return 0;

	case MODULE_CMD_AUTOUNLOAD:
		/* Couldn't unload automatically */
		return EBUSY;

	default:
		return ENOTTY;
	}
}
