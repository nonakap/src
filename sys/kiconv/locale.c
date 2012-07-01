/* $NetBSD$ */
/* NetBSD: current_locale.c,v 1.2 2009/01/11 02:46:28 christos Exp */
/* NetBSD: global_locale.c,v 1.11 2010/06/19 13:26:52 tnozaki Exp */

/*-
 * Copyright (c)2008 Citrus Project,
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("NetBSD: current_locale.c,v 1.2 2009/01/11 02:46:28 christos Exp");
__RCSID("NetBSD: global_locale.c,v 1.11 2010/06/19 13:26:52 tnozaki Exp");
#endif /* LIBC_SCCS and not lint */
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/types.h>

#include <kiconv/kiconv.h>

#include "runetype_local.h"
#include "setlocale_local.h"

const char *_PathLocale = _PATH_LOCALE;
size_t __mb_cur_max = 1;
size_t __mb_len_max_runtime = MB_LEN_MAX;

static struct _klocale_impl_t *__current_locale = &_global_locale;

struct _klocale_impl_t **
_current_locale(void)
{

	return &__current_locale;
}

static struct _klocale_cache_t _global_cache = {
    .ctype_tab   = (const unsigned char *)&_C_ctype_[0],
    .tolower_tab = (const short *)&_C_tolower_[0],
    .toupper_tab = (const short *)&_C_toupper_[0],
    .mb_cur_max = (size_t)1,
};

struct _klocale_impl_t _global_locale = {
    .cache = &_global_cache,
    .query = { _C_LOCALE },
    .part_name = {
	[(size_t)LC_CTYPE] = _C_LOCALE,
    },
    .part_impl = {
	[(size_t)LC_CTYPE] = (_klocale_part_t)
	    __UNCONST(&_DefaultRuneLocale),
    },
};
