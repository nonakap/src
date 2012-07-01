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
#include <sys/namei.h>
#include <sys/module.h>

#include <kiconv/kiconv.h>

#include "citrus_namespace.h"
#include "citrus_module.h"

void *
_citrus_find_getops(_citrus_module_t handle, const char *modname,
		    const char *ifname)
{
	struct kiconv_getops *getops;
	char *name;

	KDASSERT(modname != NULL);
	KDASSERT(ifname != NULL);

	name = PNBUF_GET();
	KASSERT(name != NULL);
	snprintf(name, PATH_MAX, "%s_%s", modname, ifname);
	getops = kiconv_find_getops(name);
	PNBUF_PUT(name);

	return getops->kg_getops;
}

int
_citrus_load_module(_citrus_module_t *rhandle, const char *encname)
{
	char *modname;
	int error;

	KDASSERT(rhandle != NULL);
	KDASSERT(encname != NULL);

	modname = malloc(MAXMODNAME, M_KICONV, M_WAITOK);
	snprintf(modname, MAXMODNAME, "kiconv_%s", encname);

#if 0
	error = module_hold(modname);
	if (error) {
		if (error != ENOENT)
			goto error;

		error = module_autoload(modname, MODULE_CLASS_MISC);
		if (error != 0 && error != EEXIST)
			goto error;
		error = module_hold(modname);
		if (error)
			goto error;
	}
#else
	error = module_autoload(modname, MODULE_CLASS_MISC);
	if (error != 0 && error != EEXIST)
		goto error;
#endif

	*rhandle = (_citrus_module_t)modname;
	return 0;

error:
	printf("kiconv: couldn't load module (%s)\n", modname);
	free(modname, M_KICONV);
	return error;
}

void
_citrus_unload_module(_citrus_module_t handle)
{

	if (handle) {
#if 0
		(void)module_rele((const char *)handle);
#endif
		free(handle, M_KICONV);
	}
}
