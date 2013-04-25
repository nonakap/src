/*	$NetBSD$	*/

/*-
 * Copyright (C) 2013 NONAKA Kimihiro <nonaka@netbsd.org>
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

#include <machine/asm.h>
#include <arm/armreg.h>
#include <evbarm/uarm/uarm_reg.h>

RCSID("$NetBSD$")

	.text
	.global	_C_LABEL(uarm_hvcall0)
_C_LABEL(uarm_hvcall0):
	mov	r12, r0
	.word	HVCALL
	mov	pc, lr

	.global	_C_LABEL(uarm_hvcall1)
_C_LABEL(uarm_hvcall1):
	mov	r12, r1
	.word	HVCALL
	mov	pc, lr

	.global	_C_LABEL(uarm_hvcall2)
_C_LABEL(uarm_hvcall2):
	mov	r12, r2
	.word	HVCALL
	mov	pc, lr

	.global	_C_LABEL(uarm_hvcall3)
_C_LABEL(uarm_hvcall3):
	mov	r12, r3
	.word	HVCALL
	mov	pc, lr
