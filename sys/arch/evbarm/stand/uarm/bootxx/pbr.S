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

#ifdef BOOT_FROM_FAT
#define MBR_AFTERBPB	90		/* BPB size in FAT32 partition BR */
#else
#define MBR_AFTERBPB	62		/* BPB size in floppy master BR */
#endif

#define	HYPERCALL	0xF7BBBBBB
#define	SECSIZE		512

	.text

	.code 32
        .global _C_LABEL(start)
_C_LABEL(start):
	b	start0
	.ascii	"NetBSD6"

	. = _C_LABEL(start) + MBR_BPB_OFFSET	/* move to start of BPB */

	. = _C_LABEL(start) + MBR_AFTERBPB	/* skip BPB */
start0:
	adr	r0, mbr_end
	ldrh	r1, mbr_size
	sub	r0, r1
	mov	r9, r0			/* r9: start address */

	/* set stack pointer */
	ldrh	r1, stack_offset
	add	r1, r0
	mov	sp, r1
	mov	r10, sp			/* r10: PBR load address */

	/* search bootable partition */
	ldrh	r11, part_offset
	add	r11, r9			/* r11: pointer to partition entry */
	mov	r8, #MBR_PART_COUNT	/* r8: partition loop counter */
loop_part:
	ldrb	r0, [r11, #4]
	cmp	r0, #MBR_PTYPE_UNUSED
	beq	next_part

	/* check active partition */
	ldrb	r0, [r11, #0]
	cmp	r0, #0x80
	bne	next_part

	/* found bootable partition */
	ldrh	r0, [r11, #8]		/* fetch unaligned 32bit data */
	mov	r1, r0
	ldrh	r0, [r11, #10]
	lsl	r0, #16
	orr	r0, r1

	adr	r1, found_sector
	str	r0, [r1]
	b	boot_lba

next_part:
	subs	r8, #1
	add	r11, #16
	bgt	loop_part

noos_error:
	/* Not found bootable partition */
	adr	r0, ERR_NOOS
error:
	bl	puts
99:	b	99b

	/* r0: asciiz */
puts:
	mov	r1, r0
	mov	r2, #0
	mov	r12, #2		/* hypercall: putchar */
.Lputs_loop:
	ldrb	r0, [r1, r2]
	cmp	r0, #0
	moveq	pc, lr
	.word	HYPERCALL
	add	r2, #1
	b	.Lputs_loop

	/* r0: buffer address */
	/* r1: sector */
read_sector_lba:
	mov	r3, r0
	/* read from disk */
	mov	r0, #1	/* r0: 0:size, 1:read, 2:write */
			/* r1: sector */
	mov	r12, #4
	.word	HYPERCALL

	/* read from buffer */
	mov	r4, #0
	mov	r5, #SECSIZE
.Lbuf_loop:
	mov	r1, r4
	mov	r2, #0
	mov	r12, #5
	.word	HYPERCALL
	str	r0, [r3], #4
	add	r4, r4, #1
	subs	r5, r5, #4
	bgt	.Lbuf_loop
	mov	pc,lr

boot_lba:
	/* read PBR sector */
	mov	r0, r10
	ldr	r1, found_sector
	bl	read_sector_lba

	/* check signature */
	ldrb	r0, [r10]
	cmp	r0, #0
	bne	noos_error
	ldrh	r0, magic_offset
	ldrh	r1, [r10, r0]
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	noos_error	/* bad magic */

	/* now jump to PBR */
	bx	r10

	.align	1
mbr_size:	.short	mbr_end - _C_LABEL(start)
	.align	1
stack_offset:	.short	0x1000
	.align	1
part_offset:	.short	MBR_PART_OFFSET
	.align	1
magic_offset:	.short	MBR_MAGIC_OFFSET

	.align	2
found_sector:	.word	0

	.align	2
banner:		.asciz	"\r\nNetBSD MBR boot\r\n"
	.align	2
ERR_NOOS:	.asciz	"No operating system\r\n"


	.align 1
/* space for mbr_dsn */
	. = _C_LABEL(start) + MBR_DSN_OFFSET
	.word	0

/* mbr_bootsel_magic */
	. = _C_LABEL(start) + MBR_BS_MAGIC_OFFSET
	.short	MBR_BS_MAGIC

/*
 * MBR partition table
 */
	. = _C_LABEL(start) + MBR_PART_OFFSET
_pbr_part0:
	.byte	0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0
_pbr_part1:
	.byte	0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0
_pbr_part2:
	.byte	0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0
_pbr_part3:
	.byte	0, 0, 0, 0, 0, 0, 0, 0
	.byte	0, 0, 0, 0, 0, 0, 0, 0

	. = _C_LABEL(start) + MBR_MAGIC_OFFSET
magic:
	.short	MBR_MAGIC
mbr_end:
