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
#include <sys/bootblock.h>
#include <evbarm/uarm/uarm_reg.h>

#define	SECSIZE	512

	.text

	.thumb
        .global _C_LABEL(start)
_C_LABEL(start):
	adr	r0, .Lstart
	bx	r0

	.align 4
	.code 32
.Lstart:
	/* banner */
	mov	r0, #'H'
	bl	.Lputchar
	mov	r0, #'e'
	bl	.Lputchar
	mov	r0, #'l'
	bl	.Lputchar
	mov	r0, #'l'
	bl	.Lputchar
	mov	r0, #'o'
	bl	.Lputchar
	mov	r0, #','
	bl	.Lputchar
	mov	r0, #' '
	bl	.Lputchar
	mov	r0, #'N'
	bl	.Lputchar
	mov	r0, #'e'
	bl	.Lputchar
	mov	r0, #'t'
	bl	.Lputchar
	mov	r0, #'B'
	bl	.Lputchar
	mov	r0, #'S'
	bl	.Lputchar
	mov	r0, #'D'
	bl	.Lputchar
	mov	r0, #'!'
	bl	.Lputchar
	mov	r0, #'\r'
	bl	.Lputchar
	mov	r0, #'\n'
	bl	.Lputchar

	/* load kernel */
	ldr	r8, .Lkernel_text
	mov	r9, #64
	ldr	r10, .Lkernel_secsize
1:
	mov	r0, r8
	mov	r1, r9
	bl	.Lreadsec
	mov	r0, #'.'
	bl	.Lputchar
	add	r8, r8, #SECSIZE
	add	r9, r9, #1
	subs	r10, r10, #1
	bgt	1b

	/* banner */
	mov	r0, #'B'
	bl	.Lputchar
	mov	r0, #'o'
	bl	.Lputchar
	mov	r0, #'o'
	bl	.Lputchar
	mov	r0, #'t'
	bl	.Lputchar
	mov	r0, #'\r'
	bl	.Lputchar
	mov	r0, #'\n'
	bl	.Lputchar

	/* jump into kernel */
	ldr	r0, .Lkernel_text
	bx	r0

	mov	r12, #HV_OP_RESET
	.word	HVCALL
99:	b	99b

.Lkernel_text:
	.word	0xa0200000
.Lkernel_secsize:
	.word	12229-64

	/* r0: char */
.Lputchar:
	mov	r12, #HV_OP_PUTCHAR
	.word	HVCALL
	mov	pc, lr

	/* r0: buffer address */
	/* r1: sector */
.Lreadsec:
	mov	r3, r0
	/* read from disk */
	mov	r0, #HV_DEVICE_OP_READ
	mov	r12, #HV_OP_DEVICE
	.word	HVCALL

	/* read from buffer */
	mov	r4, #0
	mov	r5, #SECSIZE
1:
	mov	r1, r4
	mov	r2, #HV_BUFFER_OP_READ
	mov	r12, #HV_OP_BUFFER
	.word	HVCALL
	str	r0, [r3], #4
	add	r4, r4, #1
	subs	r5, r5, #4
	bgt	1b
	mov	pc,lr


	.align 1
/*
 * MBR partition table
 */
	. = _C_LABEL(start) + MBR_PART_OFFSET
_pbr_part0:
	.byte	0x80, 0x46, 0x06, 0x01, 0xa9, 0xcf, 0x0c, 0xc3
	.byte	0x00, 0x50, 0x00, 0x00, 0x00, 0xb0, 0x2f, 0x00
_pbr_part1:
	.byte	0x00, 0xcf, 0x0d, 0xc3, 0x06, 0x15, 0x50, 0x00
	.byte	0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x10, 0x00
_pbr_part2:
	.byte	0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0
_pbr_part3:
	.byte	0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0

	. = _C_LABEL(start) + MBR_MAGIC_OFFSET
magic:
	.short	MBR_MAGIC
	.space	512 * 63
