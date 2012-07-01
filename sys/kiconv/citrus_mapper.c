/*	$NetBSD$	*/
/*	NetBSD: citrus_mapper.c,v 1.7 2008/07/25 14:05:25 christos Exp	*/

/*-
 * Copyright (c)2003 Citrus Project,
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
__RCSID("NetBSD: citrus_mapper.c,v 1.7 2008/07/25 14:05:25 christos Exp");
#endif /* LIBC_SCCS and not lint */
__KERNEL_RCSID(0, "$NetBSD$");

#ifdef	_KERNEL
#include <sys/param.h>
#include <sys/types.h>
#include <sys/rwlock.h>
#include <sys/filedesc.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/kauth.h>
#include <sys/proc.h>
#include <sys/fcntl.h>

#include <kiconv/kiconv.h>
#else
#include "namespace.h"
#include "reentrant.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>
#endif

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_region.h"
#include "citrus_memstream.h"
#include "citrus_bcs.h"
#include "citrus_mmap.h"
#include "citrus_module.h"
#include "citrus_hash.h"
#include "citrus_mapper.h"

#define _CITRUS_MAPPER_DIR	"mapper.dir"

#define CM_HASH_SIZE 101
#define REFCOUNT_PERSISTENT	-1

krwlock_t kiconv_mapper_lock __cacheline_aligned;

struct _citrus_mapper_area {
	_CITRUS_HASH_HEAD(, _citrus_mapper, CM_HASH_SIZE)	ma_cache;
	char							*ma_dir;
};


/*
 * _citrus_mapper_create_area:
 *	create mapper area
 */

int
_citrus_mapper_create_area(
	struct _citrus_mapper_area *__restrict *__restrict rma,
	const char *__restrict area)
{
	struct _citrus_mapper_area *ma;
	struct pathbuf *pb;
	struct nameidata nd;
	struct vattr va;
	char *path;
	struct vnode *vp;
	int error;
	extern struct cwdinfo cwdi0;

	if (*rma != NULL)
		return 0;

	if (cwdi0.cwdi_cdir == NULL) {
		printf("_citrus_mapper_create_area(%s/%s) call too early.\n",
		    area, _CITRUS_MAPPER_DIR);
		return ENOENT;
	}

	path = PNBUF_GET();
	KASSERT(path != NULL);
	snprintf(path, PATH_MAX, "%s/%s", area, _CITRUS_MAPPER_DIR);
	pb = pathbuf_create(path);
	PNBUF_PUT(path);
	if (pb == NULL) {
		error = ENOMEM;
		goto out;
	}
	NDINIT(&nd, LOOKUP, FOLLOW | NOCHROOT, pb);
	error = vn_open(&nd, FREAD, 0);
	pathbuf_destroy(pb);
	if (error)
		goto out;

	vp = nd.ni_vp;

	error = VOP_GETATTR(vp, &va, kauth_cred_get());
	if (error)
		goto res_free;

	if (va.va_type != VREG) {
		error = EINVAL;
		goto res_free;
	}

	ma = malloc(sizeof(*ma), M_KICONV, M_WAITOK);
	ma->ma_dir = strdup(area);
	_CITRUS_HASH_INIT(&ma->ma_cache, CM_HASH_SIZE);
	*rma = ma;

res_free:
	VOP_UNLOCK(vp);
	(void)vn_close(vp, FREAD, kauth_cred_get());
out:
	return error;
}


/*
 * lookup_mapper_entry:
 *	lookup mapper.dir entry in the specified directory.
 *
 * line format of iconv.dir file:
 *	mapper	module	arg
 * mapper : mapper name.
 * module : mapper module name.
 * arg    : argument for the module (generally, description file name)
 */

static int
lookup_mapper_entry(const char *dir, const char *mapname,
		    void *linebuf, size_t linebufsize,
		    const char **module, const char **variable)
{
	struct _region r;
	struct _memstream ms;
	int ret;
	const char *cp, *cq;
	char *p;
	size_t len;
	char *path;

	path = PNBUF_GET();
	KASSERT(path != NULL);

	/* create mapper.dir path */
	snprintf(path, PATH_MAX, "%s/%s", dir, _CITRUS_MAPPER_DIR);

	/* open read stream */
	ret = _map_file(&r, path);
	PNBUF_PUT(path);
	if (ret)
		return ret;

	_memstream_bind(&ms, &r);

	/* search the line matching to the map name */
	cp = _memstream_matchline(&ms, mapname, &len, 0);
	if (!cp) {
		ret = ENOENT;
		goto quit;
	}
	if (!len || len>linebufsize-1) {
		ret = EINVAL;
		goto quit;
	}

	p = linebuf;
	/* get module name */
	*module = p;
	cq = _bcs_skip_nonws_len(cp, &len);
	memcpy(p, cp, cq-cp);
	p[cq-cp] = '\0';
	p += cq-cp+1;

	/* get variable */
	*variable = p;
	cp = _bcs_skip_ws_len(cq, &len);
	memcpy(p, cp, len);
	p[len] = '\0';

quit:
	_unmap_file(&r);
	return ret;
}

/*
 * mapper_close:
 *	simply close a mapper. (without handling hash)
 */
static void
mapper_close(struct _citrus_mapper *cm)
{

	if (cm->cm_module) {
		if (cm->cm_ops) {
			if (cm->cm_closure)
				(*cm->cm_ops->mo_uninit)(cm);
			free(cm->cm_ops, M_KICONV);
		}
		_citrus_unload_module(cm->cm_module);
	}
	if (cm->cm_traits)
		free(cm->cm_traits, M_KICONV);
	free(cm, M_KICONV);
}

/*
 * mapper_open:
 *	simply open a mapper. (without handling hash)
 */
static int
mapper_open(struct _citrus_mapper_area *__restrict ma,
	    struct _citrus_mapper * __restrict * __restrict rcm,
	    const char * __restrict module,
	    const char * __restrict variable)
{
	int ret;
	struct _citrus_mapper *cm;
	_citrus_mapper_getops_t getops;

	/* initialize mapper handle */
	cm = malloc(sizeof(*cm), M_KICONV, M_WAITOK);
	cm->cm_module = NULL;
	cm->cm_ops = NULL;
	cm->cm_closure = NULL;
	cm->cm_traits = NULL;
	cm->cm_refcount = 0;
	cm->cm_key = NULL;

	/* load module */
	ret = _citrus_load_module(&cm->cm_module, module);
	if (ret)
		goto err;

	/* get operators */
	getops = (_citrus_mapper_getops_t)
	    _citrus_find_getops(cm->cm_module, module, "mapper");
	if (getops == NULL) {
		ret = EOPNOTSUPP;
		goto err;
	}
	cm->cm_ops = malloc(sizeof(*cm->cm_ops), M_KICONV, M_WAITOK);
	ret = (*getops)(cm->cm_ops, sizeof(*cm->cm_ops),
			_CITRUS_MAPPER_ABI_VERSION);
	if (ret)
		goto err;

	if (!cm->cm_ops->mo_init ||
	    !cm->cm_ops->mo_uninit ||
	    !cm->cm_ops->mo_convert ||
	    !cm->cm_ops->mo_init_state) {
		ret = EINVAL;
		goto err;
	}

	/* allocate traits structure */
	cm->cm_traits = malloc(sizeof(*cm->cm_traits), M_KICONV, M_WAITOK);

	/* initialize the mapper */
	ret = (*cm->cm_ops->mo_init)(ma, cm, ma->ma_dir,
				     (const void *)variable,
				     strlen(variable)+1,
				     cm->cm_traits, sizeof(*cm->cm_traits));
	if (ret)
		goto err;

	*rcm = cm;

	return 0;

err:
	mapper_close(cm);
	return ret;
}

/*
 * _citrus_mapper_open_direct:
 *	open a mapper.
 */
int
_citrus_mapper_open_direct(struct _citrus_mapper_area *__restrict ma,
			   struct _citrus_mapper * __restrict * __restrict rcm,
			   const char * __restrict module,
			   const char * __restrict variable)
{
	return mapper_open(ma, rcm, module, variable);
}

/*
 * hash_func
 */
static __inline int
hash_func(const char *key)
{
	return _string_hash_func(key, CM_HASH_SIZE);
}

/*
 * match_func
 */
static __inline int
match_func(struct _citrus_mapper *cm, const char *key)
{
	return strcmp(cm->cm_key, key);
}

/*
 * _citrus_mapper_open:
 *	open a mapper with looking up "mapper.dir".
 */
int
_citrus_mapper_open(struct _citrus_mapper_area *__restrict ma,
		    struct _citrus_mapper * __restrict * __restrict rcm,
		    const char * __restrict mapname)
{
	int ret;
	char *linebuf;
	const char *module, *variable = NULL;
	struct _citrus_mapper *cm;
	int hashval;

	linebuf = KICONV_LINEBUF_GET();
	KASSERT(linebuf != NULL);

	rw_enter(&kiconv_mapper_lock, RW_READER);

	/* search in the cache */
	hashval = hash_func(mapname);
	_CITRUS_HASH_SEARCH(&ma->ma_cache, cm, cm_entry, match_func, mapname,
			    hashval);
	if (cm) {
		/* found */
		cm->cm_refcount++;
		*rcm = cm;
		ret = 0;
		goto unlock;
	}

	rw_exit(&kiconv_mapper_lock);

	/* search mapper entry */
	ret = lookup_mapper_entry(ma->ma_dir, mapname, linebuf, LINE_MAX,
	    &module, &variable);
	if (ret)
		goto out;

	/* open mapper */
	ret = mapper_open(ma, &cm, module, variable);
	if (ret)
		goto out;
	cm->cm_key = strdup(mapname);

	/* insert to the cache */
	cm->cm_refcount = 1;
	rw_enter(&kiconv_mapper_lock, RW_WRITER);
	_CITRUS_HASH_INSERT(&ma->ma_cache, cm, cm_entry, hashval);

	*rcm = cm;
unlock:
	rw_exit(&kiconv_mapper_lock);
out:
	KICONV_LINEBUF_PUT(linebuf);
	return ret;
}

/*
 * _citrus_mapper_close:
 *	close the specified mapper.
 */
void
_citrus_mapper_close(struct _citrus_mapper *cm)
{

	if (cm) {
		rw_enter(&kiconv_mapper_lock, RW_READER);
		if (cm->cm_refcount == REFCOUNT_PERSISTENT)
			goto quit;
		if (cm->cm_refcount > 0) {
			if (--cm->cm_refcount > 0)
				goto quit;
			while (rw_tryupgrade(&kiconv_mapper_lock) == 0)
				kpause("kicmap", true, 1, NULL);
			_CITRUS_HASH_REMOVE(cm, cm_entry);
			rw_downgrade(&kiconv_mapper_lock);
			if (cm->cm_key)
				free(cm->cm_key, M_KICONV);
		}
		rw_exit(&kiconv_mapper_lock);
		mapper_close(cm);
		return;
quit:
		rw_exit(&kiconv_mapper_lock);
	}
}

/*
 * _citrus_mapper_set_persistent:
 *	set persistent count.
 */
void
_citrus_mapper_set_persistent(struct _citrus_mapper * __restrict cm)
{

	rw_enter(&kiconv_mapper_lock, RW_WRITER);
	cm->cm_refcount = REFCOUNT_PERSISTENT;
	rw_exit(&kiconv_mapper_lock);
}
