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

#include "opt_kiconv.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/pool.h>
#include <sys/rwlock.h>

#define	_KICONV_LOCAL
#include <kiconv/kiconv.h>

#define	MODNAME	"kiconv"

int kiconv_debug = 0;

MALLOC_DEFINE(M_KICONV, "kiconv", "in-kernel iconv buffer");
pool_cache_t kiconv_wchar_pathbuf_cache;
pool_cache_t kiconv_linebuf_cache;

TAILQ_HEAD(kglist, kiconv_getops);
static struct kglist allkg = TAILQ_HEAD_INITIALIZER(allkg);
static struct kiconv_ops *ops;
static kmutex_t getops_lock __cacheline_aligned;
static krwlock_t ops_lock __cacheline_aligned;
static bool kiconv_inited;

static __inline void kiconv_attach_getops1(struct kiconv_getops *);

void
kiconv_init(void)
{

	if (kiconv_inited)
		return;

	mutex_init(&getops_lock, MUTEX_DEFAULT, IPL_NONE);
	rw_init(&ops_lock);

	kiconv_wchar_pathbuf_cache = pool_cache_init(PATH_MAX * sizeof(wchar_t),
	    0, 0, 0, "wpnbuf", NULL, IPL_NONE, NULL, NULL, NULL);
	KASSERT(kiconv_wchar_pathbuf_cache != NULL);
	kiconv_linebuf_cache = pool_cache_init(LINE_MAX, 0, 0, 0, "linebufpl",
	    NULL, IPL_NONE, NULL, NULL, NULL);
	KASSERT(kiconv_linebuf_cache != NULL);

	kiconv_inited = true;
}

/*
 * kiconv ops
 */
void
kiconv_attach_ops(struct kiconv_ops *ko)
{

	KASSERT(kiconv_inited);

	rw_enter(&ops_lock, RW_WRITER);
	ops = ko;
	rw_exit(&ops_lock);
}

void
kiconv_detach_ops(struct kiconv_ops *ko)
{

	KASSERT(kiconv_inited);

	rw_enter(&ops_lock, RW_WRITER);
	if (ops == ko)
		ops = NULL;
	rw_exit(&ops_lock);
}

/*
 * module/iconv/mapper getops
 */
static __inline void
kiconv_attach_getops1(struct kiconv_getops *kg)
{

	mutex_enter(&getops_lock);
	TAILQ_INSERT_TAIL(&allkg, kg, kg_list);
	mutex_exit(&getops_lock);
}

void
kiconv_attach_getops(struct kiconv_getops *kg)
{

	KASSERT(kiconv_inited);

	kiconv_attach_getops1(kg);
}

void
kiconv_detach_getops(struct kiconv_getops *kg)
{

	KASSERT(kiconv_inited);

	mutex_enter(&getops_lock);
	TAILQ_REMOVE(&allkg, kg, kg_list);
	mutex_exit(&getops_lock);
}

struct kiconv_getops *
kiconv_find_getops(const char *name)
{
	struct kiconv_getops *kg;

	KASSERT(kiconv_inited);

	mutex_enter(&getops_lock);
	TAILQ_FOREACH(kg, &allkg, kg_list) {
		if (strcmp(name, kg->kg_name) == 0) {
			mutex_exit(&getops_lock);
			return kg;
		}
	}
	mutex_exit(&getops_lock);
	return NULL;
}

#if !defined(KICONV) || (KICONV == 0)
/*
 * modular version
 */
kiconv_t
kiconv_open(const char *out, const char *in, int *errnop)
{
	bool retried = false;
	kiconv_t handle;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		module_hold(MODNAME);
		handle = (*ops->iconv_open)(out, in, errnop);
		if (handle == (kiconv_t)-1)
			module_rele(MODNAME);
		rw_exit(&ops_lock);
		return handle;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (kiconv_t)-1;
}

int
kiconv_close(kiconv_t handle, int *errnop)
{
	bool retried = false;
	int result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->iconv_close)(handle, errnop);
		if (result == 0)
			module_rele(MODNAME);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return -1;
}

size_t
kiconv_conv(kiconv_t handle, const char **in, size_t *szin, char **out,
    size_t *szout, int *errnop)
{
	bool retried = false;
	size_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->iconv)(handle, in, szin, out, szout, 0,
		    NULL, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (size_t)-1;
}

size_t
__kiconv_conv(kiconv_t handle, const char **in, size_t *szin, char **out,
    size_t *szout, uint32_t flags, size_t *invalids, int *errnop)
{
	bool retried = false;
	size_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->iconv)(handle, in, szin, out, szout,
		    flags, invalids, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (size_t)-1;
}

klocale_t
kiconv_newlocale(const char *locale, int *errnop)
{
	bool retried = false;
	klocale_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		module_hold(MODNAME);
		result = (*ops->newlocale)(locale, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (klocale_t)NULL;
}

int
kiconv_freelocale(klocale_t locale, int *errnop)
{
	bool retried = false;
	int result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->freelocale)(locale, errnop);
		if (result == 0)
			module_rele(MODNAME);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return -1;
}

size_t
kiconv_mbrlen_l(const char *s, size_t n, mbstate_t *ps, klocale_t locale,
    int *errnop)
{
	bool retried = false;
	size_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->mbrlen)(s, n, ps, locale, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (size_t)-1;
}

int
kiconv_mbsinit_l(const mbstate_t *ps, klocale_t locale, int *errnop)
{
	bool retried = false;
	int result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->mbsinit)(ps, locale, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return -1;
}

size_t
kiconv_mbrtowc_l(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps,
    klocale_t locale, int *errnop)
{
	bool retried = false;
	size_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->mbrtowc)(pwc, s, n, ps, locale, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (size_t)-1;
}

size_t
kiconv_mbsrtowcs_l(wchar_t *pwcs, const char **s, size_t n, mbstate_t *ps,
    klocale_t locale, int *errnop)
{
	bool retried = false;
	size_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->mbsrtowcs)(pwcs, s, n, ps, locale, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (size_t)-1;
}

size_t
kiconv_wcrtomb_l(char *s, wchar_t wc, mbstate_t *ps, klocale_t locale,
    int *errnop)
{
	bool retried = false;
	size_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->wcrtomb)(s, wc, ps, locale, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (size_t)-1;
}

size_t
kiconv_wcsrtombs_l(char *s, const wchar_t **ppwcs, size_t n, mbstate_t *ps,
    klocale_t locale, int *errnop)
{
	bool retried = false;
	size_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->wcsrtombs)(s, ppwcs, n, ps, locale, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (size_t)-1;
}

wint_t
kiconv_btowc_l(int c, klocale_t locale, int *errnop)
{
	bool retried = false;
	wint_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->btowc)(c, locale, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return (wint_t)-1;
}

int
kiconv_wctob_l(wint_t wc, klocale_t locale, int *errnop)
{
	bool retried = false;
	int result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->wctob)(wc, locale, errnop);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	if (errnop)
		*errnop = ENOENT;
	return -1;
}

int
kiconv_wcwidth_l(wchar_t wc, klocale_t locale)
{
	bool retried = false;
	int result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->wcwidth)(wc, locale);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	return -1;
}

int
kiconv_wcswidth_l(const wchar_t *wcs, size_t wn, klocale_t locale)
{
	bool retried = false;
	int result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->wcswidth)(wcs, wn, locale);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	return -1;
}

#define	_KICONV_ISWCTYPE_FUNC(name)					\
int									\
kiconv_isw##name##_l(wint_t wc, klocale_t locale)			\
{									\
	bool retried = false;						\
	int result;							\
									\
	KASSERT(kiconv_inited);						\
									\
retry:									\
	rw_enter(&ops_lock, RW_READER);					\
	if (ops) {							\
		result = (*ops->isw##name)(wc, locale);			\
		rw_exit(&ops_lock);					\
		return result;						\
	}								\
	rw_exit(&ops_lock);						\
	if (!retried) {							\
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);	\
		retried = true;						\
		goto retry;						\
	}								\
									\
	printf("%s: kiconv module couldn't load.", __func__);		\
	return -1;							\
}
_KICONV_ISWCTYPE_FUNC(alnum)
_KICONV_ISWCTYPE_FUNC(alpha)
_KICONV_ISWCTYPE_FUNC(blank)
_KICONV_ISWCTYPE_FUNC(cntrl)
_KICONV_ISWCTYPE_FUNC(graph)
_KICONV_ISWCTYPE_FUNC(lower)
_KICONV_ISWCTYPE_FUNC(print)
_KICONV_ISWCTYPE_FUNC(punct)
_KICONV_ISWCTYPE_FUNC(space)
_KICONV_ISWCTYPE_FUNC(upper)
_KICONV_ISWCTYPE_FUNC(xdigit)

#define	_KICONV_TOWCTYPE_FUNC(name)					\
wint_t									\
kiconv_tow##name##_l(wint_t wc, klocale_t locale)			\
{									\
	bool retried = false;						\
	wint_t result;							\
									\
	KASSERT(kiconv_inited);						\
									\
retry:									\
	rw_enter(&ops_lock, RW_READER);					\
	if (ops) {							\
		result = (*ops->tow##name)(wc, locale);			\
		rw_exit(&ops_lock);					\
		return result;						\
	}								\
	rw_exit(&ops_lock);						\
	if (!retried) {							\
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);	\
		retried = true;						\
		goto retry;						\
	}								\
									\
	printf("%s: kiconv module couldn't load.", __func__);		\
	return (wint_t)-1;						\
}
_KICONV_TOWCTYPE_FUNC(upper)
_KICONV_TOWCTYPE_FUNC(lower)

wctype_t
kiconv_wctype_l(const char *property, klocale_t locale)
{
	bool retried = false;
	wctype_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->wctype)(property, locale);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	return (wctype_t)0;
}

wctrans_t
kiconv_wctrans_l(const char *property, klocale_t locale)
{
	bool retried = false;
	wctrans_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->wctrans)(property, locale);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	return (wctrans_t)0;
}

int
kiconv_iswctype_l(wint_t wc, wctype_t desc, klocale_t locale)
{
	bool retried = false;
	int result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->iswctype)(wc, desc, locale);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	return 0;
}

wint_t
kiconv_towctrans_l(wint_t wc, wctrans_t desc, klocale_t locale)
{
	bool retried = false;
	wint_t result;

	KASSERT(kiconv_inited);

retry:
	rw_enter(&ops_lock, RW_READER);
	if (ops) {
		result = (*ops->towctrans)(wc, desc, locale);
		rw_exit(&ops_lock);
		return result;
	}
	rw_exit(&ops_lock);
	if (!retried) {
		(void)module_autoload(MODNAME, MODULE_CLASS_MISC);
		retried = true;
		goto retry;
	}

	printf("%s: kiconv module couldn't load.", __func__);
	return (wint_t)WEOF;	/* XXX */
}
#else	/* KICONV && (KICONV > 0) */
/*
 * static link version
 */
kiconv_t
kiconv_open(const char *out, const char *in, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_open(out, in, errnop);
}

int
kiconv_close(kiconv_t handle, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_close(handle, errnop);
}

size_t
kiconv_conv(kiconv_t handle, const char **in, size_t *szin, char **out,
    size_t *szout, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_conv(handle, in, szin, out, szout, 0, NULL, errnop);
}

size_t
__kiconv_conv(kiconv_t handle, const char **in, size_t *szin, char **out,
    size_t *szout, uint32_t flags, size_t *invalids, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_conv(handle, in, szin, out, szout, flags, invalids,
	    errnop);
}

klocale_t
kiconv_newlocale(const char *locale, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_newlocale(locale, errnop);
}

int
kiconv_freelocale(klocale_t locale, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_freelocale(locale, errnop);
}

size_t
kiconv_mbrlen_l(const char *s, size_t n, mbstate_t *ps, klocale_t locale,
    int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_mbrlen(s, n, ps, locale, errnop);
}

int
kiconv_mbsinit_l(const mbstate_t *ps, klocale_t locale, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_mbsinit(ps, locale, errnop);
}

size_t
kiconv_mbrtowc_l(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps,
    klocale_t locale, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_mbrtowc(pwc, s, n, ps, locale, errnop);
}

size_t
kiconv_mbsrtowcs_l(wchar_t *pwcs, const char **s, size_t n, mbstate_t *ps,
    klocale_t locale, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_mbsrtowcs(pwcs, s, n, ps, locale, errnop);
}

size_t
kiconv_wcrtomb_l(char *s, wchar_t wc, mbstate_t *ps, klocale_t locale,
    int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_wcrtomb(s, wc, ps, locale, errnop);
}

size_t
kiconv_wcsrtombs_l(char *s, const wchar_t **ppwcs, size_t n, mbstate_t *ps,
    klocale_t locale, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_wcsrtombs(s, ppwcs, n, ps, locale, errnop);
}

wint_t
kiconv_btowc_l(int c, klocale_t locale, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_btowc(c, locale, errnop);
}

int
kiconv_wctob_l(wint_t wc, klocale_t locale, int *errnop)
{

	KASSERT(kiconv_inited);
	return _kiconv_wctob(wc, locale, errnop);
}

int
kiconv_wcwidth_l(wchar_t wc, klocale_t locale)
{

	KASSERT(kiconv_inited);
	return _kiconv_wcwidth(wc, locale);
}

int
kiconv_wcswidth_l(const wchar_t *wcs, size_t wn, klocale_t locale)
{

	KASSERT(kiconv_inited);
	return _kiconv_wcswidth(wsc, wn, locale);
}

#define	_KICONV_ISWCTYPE_FUNC(name)					\
int									\
kiconv_isw##name##_l(wint_t wc, klocale_t locale)			\
{									\
									\
	KASSERT(kiconv_inited);						\
	return _kiconv_isw##name(wc, locale);				\
}
_KICONV_ISWCTYPE_FUNC(alnum)
_KICONV_ISWCTYPE_FUNC(alpha)
_KICONV_ISWCTYPE_FUNC(blank)
_KICONV_ISWCTYPE_FUNC(cntrl)
_KICONV_ISWCTYPE_FUNC(graph)
_KICONV_ISWCTYPE_FUNC(lower)
_KICONV_ISWCTYPE_FUNC(print)
_KICONV_ISWCTYPE_FUNC(punct)
_KICONV_ISWCTYPE_FUNC(space)
_KICONV_ISWCTYPE_FUNC(upper)
_KICONV_ISWCTYPE_FUNC(xdigit)

#define	_KICONV_TOWCTYPE_FUNC(name)					\
wint_t									\
kiconv_tow##name##_l(wint_t wc, klocale_t locale)			\
{									\
									\
	KASSERT(kiconv_inited);						\
	return _kiconv_tow##name(wc, locale);				\
}
_KICONV_TOWCTYPE_FUNC(upper)
_KICONV_TOWCTYPE_FUNC(lower)

wctype_t
kiconv_wctype_l(const char *property, klocale_t locale)
{

	KASSERT(kiconv_inited);
	return _kiconv_wctype(property, locale);
}

wctrans_t
kiconv_wctrans_l(const char *property, klocale_t locale)
{

	KASSERT(kiconv_inited);
	return _kiconv_wctrans(property, locale);
}

int
kiconv_iswctype_l(wint_t wc, wctype_t desc, klocale_t locale)
{

	KASSERT(kiconv_inited);
	return _kiconv_iswctype(wc, desc, locale);
}

wint_t
kiconv_towctrans_l(wint_t wc, wctrans_t desc, klocale_t locale)
{

	KASSERT(kiconv_inited);
	return _kiconv_towctrans(wc, desc, locale);
}
#endif
