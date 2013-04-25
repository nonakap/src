/*	$NetBSD$ */

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

#ifndef _EVBARM_UARM_REG_H
#define _EVBARM_UARM_REG_H

/*
 * Logical mapping for onboard/integrated peripherals
 */
#define UARM_IO_AREA_VBASE	0xfd000000
#define UARM_GPIO_VBASE		0xfd000000
#define UARM_CLKMAN_VBASE	0xfd100000
#define UARM_INTCTL_VBASE	0xfd200000
#define UARM_FFUART_VBASE	0xfd300000
#define UARM_BTUART_VBASE	0xfd400000
#define UARM_STUART_VBASE	0xfd500000

#define ioreg_read(a)  (*(volatile unsigned *)(a))
#define ioreg_write(a,v)  (*(volatile unsigned *)(a)=(v))


/*
 * Hypervisor call
 */
#define HVCALL		0xf7bbbbbb
#define HVCALL_T	0xbbbb

/* Hypervisor Operation (r12) */
#define HV_OP_RESET		0
#define HV_OP_PUTNUMBER		1
#define HV_OP_PUTCHAR		2
#define HV_OP_GET_RAM_SIZE	3
#define HV_OP_DEVICE		4	/* block device access */
#define HV_OP_BUFFER		5	/* block device buffer access */

/*
 * block device access (buffer<->disk)
 *
 * r0: op: (I)
 * r1: sector#: (I)
 */
/* r0: op */
#define HV_DEVICE_OP_DISKINFO	0
#define HV_DEVICE_OP_READ	1
#define HV_DEVICE_OP_WRITE	2

/* r1: disk info: only for disk info */
#define HV_DEVICE_DISKINFO_BLOCK_NUMBER	0
#define HV_DEVICE_DISKINFO_BLOCK_SIZE	1

/*
 * block device buffer access (virtual machine<->buffer)
 *
 * r0: word value: read(O), write(I)
 * r1: word offset: (I)
 * r2: op: (I)
 */
/* r2: op */
#define HV_BUFFER_OP_READ	0
#define HV_BUFFER_OP_WRITE	1

#endif /* _EVBARM_UARM_REG_H */
