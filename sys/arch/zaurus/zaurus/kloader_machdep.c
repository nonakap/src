/*	$NetBSD: kloader_machdep.c,v 1.6 2012/01/21 18:56:51 nonaka Exp $	*/

/*-
 * Copyright (C) 2009-2012 NONAKA Kimihiro <nonaka@netbsd.org>
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
__KERNEL_RCSID(0, "$NetBSD: kloader_machdep.c,v 1.6 2012/01/21 18:56:51 nonaka Exp $");

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/kloader.h>
#include <machine/pmap.h>

#include <arm/cpufunc.h>
#include <arm/xscale/pxa2x0reg.h>

#include <zaurus/zaurus/zaurus_var.h>

kloader_jumpfunc_t kloader_zaurus_jump __attribute__((__noreturn__));
kloader_bootfunc_t kloader_zaurus_boot __attribute__((__noreturn__));
void kloader_zaurus_reset(void);

struct kloader_ops kloader_zaurus_ops = {
	.jump = kloader_zaurus_jump,
	.boot = kloader_zaurus_boot,
	.reset = kloader_zaurus_reset,
};

void
kloader_reboot_setup(const char *filename)
{

	__kloader_reboot_setup(&kloader_zaurus_ops, filename);
}

void
kloader_zaurus_reset(void)
{

	zaurus_restart();
	/*NOTREACHED*/
}

void
kloader_zaurus_jump(kloader_bootfunc_t func, vaddr_t sp,
    struct kloader_bootinfo *kbi, struct kloader_page_tag *tag)
{

	disable_interrupts(I32_bit|F32_bit);	
	cpu_idcache_wbinv_all();

	/* jump to 2nd boot-loader */
	(*func)(kbi, tag);
}

/*
 * Physcal address to virtual address
 */
vaddr_t
kloader_phystov(paddr_t pa)
{
	vaddr_t va;
	int error;

	va = KERNEL_BASE + pa - PXA2X0_SDRAM0_START;
	error = pmap_enter(pmap_kernel(), va, pa, VM_PROT_ALL, 0);
	if (error) {
		printf("%s: map failed: pa=0x%lx, va=0x%lx, error=%d\n",
		    __func__, pa, va, error);
	}
	return va;
}
