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

#ifndef _EVBARM_UARM_VAR_H
#define _EVBARM_UARM_VAR_H

/*
 * hypervisor
 */
uint32_t uarm_hvcall0(int hvop);
uint32_t uarm_hvcall1(uint32_t arg1, int hvop);
uint32_t uarm_hvcall2(uint32_t arg1, uint32_t arg2, int hvop);
uint32_t uarm_hvcall3(uint32_t arg1, uint32_t arg2, uint32_t arg3, int hvop);

static inline void
uarm_reset(void)
{

	uarm_hvcall0(HV_OP_RESET);
}

static inline void
uarm_putchar(int c)
{

	uarm_hvcall1(c & 0xff, HV_OP_PUTCHAR);
}

static inline uint32_t
uarm_get_memory_size(void)
{

	return uarm_hvcall0(HV_OP_GET_RAM_SIZE);
}

static inline uint32_t
uarm_get_disk_blocks(void)
{

	uarm_hvcall2(HV_DEVICE_OP_DISKINFO, HV_DEVICE_DISKINFO_BLOCK_NUMBER,
	    HV_OP_DEVICE);
	return uarm_hvcall3(0, 0, HV_BUFFER_OP_READ, HV_OP_BUFFER);
}

static inline uint32_t
uarm_get_disk_block_size(void)
{

	uarm_hvcall2(HV_DEVICE_OP_DISKINFO, HV_DEVICE_DISKINFO_BLOCK_SIZE,
	    HV_OP_DEVICE);
	return uarm_hvcall3(0, 0, HV_BUFFER_OP_READ, HV_OP_BUFFER);
}

#endif /* _EVBARM_UARM_VAR_H */
