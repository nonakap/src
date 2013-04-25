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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/kthread.h>
#if 0
#include <sys/callout.h>
#endif
#include <sys/rnd.h>

#include <dev/ldvar.h>

#include <arch/evbarm/uarm/pvbusvar.h>

#include <arch/evbarm/uarm/uarm_reg.h>
#include <arch/evbarm/uarm/uarm_var.h>

#ifdef LD_PVBUS_DEBUG_DUMP
#ifndef PVBUS_DEBUG
#define PVBUS_DEBUG
#endif	/* PVBUS_DEBUG */
#endif	/* LD_PVBUS_DEBUG_DUMP */

#ifdef PVBUS_DEBUG
#define DPRINTF(s)	printf s
#else	/* !PVBUS_DEBUG */
#define DPRINTF(s)	/**/
#endif	/* PVBUS_DEBUG */

struct ld_pvbus_softc;

struct ld_pvbus_task {
	struct pvbus_task task;

	struct ld_pvbus_softc *task_sc;
	struct buf *task_bp;
#if 0
	callout_t task_callout;
#endif
};

struct ld_pvbus_softc {
	struct ld_softc sc_ld;
	struct pvbus_softc *sc_pvbus;

	struct ld_pvbus_task sc_task;
};

static int ld_pvbus_match(device_t, cfdata_t, void *);
static void ld_pvbus_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(ld_pvbus, sizeof(struct ld_pvbus_softc),
    ld_pvbus_match, ld_pvbus_attach, NULL, NULL);

static int ld_pvbus_dump(struct ld_softc *, void *, int, int);
static int ld_pvbus_start(struct ld_softc *, struct buf *);

static void ld_pvbus_doattach(void *);
static void ld_pvbus_dobio(void *);
#if 0
static void ld_pvbus_timeout(void *);
#endif

static int ld_pvbus_read_block(struct ld_pvbus_softc *, uint32_t, void *, int);
static int ld_pvbus_write_block(struct ld_pvbus_softc *, uint32_t, void *, int);
#ifdef LD_PVBUS_DEBUG_DUMP
static void ld_pvbus_dump_data(const char *, const void *, size_t);
#endif

/*ARGSUSED*/
static int
ld_pvbus_match(device_t parent, cfdata_t match, void *aux)
{

	return (1);
}

/*ARGSUSED*/
static void
ld_pvbus_attach(device_t parent, device_t self, void *aux)
{
	struct ld_pvbus_softc *sc = device_private(self);
	struct ld_softc *ld = &sc->sc_ld;
	struct lwp *lwp;

	ld->sc_dv = self;
	sc->sc_pvbus = device_private(parent);

	aprint_normal("\n");
	aprint_naive("\n");

#if 0
	callout_init(&sc->sc_task.task_callout, CALLOUT_MPSAFE);
#endif

	ld->sc_secperunit = uarm_get_disk_blocks();
	ld->sc_secsize = uarm_get_disk_block_size();
	KASSERT((ld->sc_secsize & 3) == 0);
	ld->sc_maxxfer = MAXPHYS;
	ld->sc_maxqueuecnt = 1;
	ld->sc_dump = ld_pvbus_dump;
	ld->sc_start = ld_pvbus_start;
	ld->sc_flags = LDF_ENABLED;

	/*
	 * It is avoided that the error occurs when the card attaches it,
	 * when wedge is supported.
	 */
	config_pending_incr();
	if (kthread_create(PRI_NONE, KTHREAD_MPSAFE, NULL,
	    ld_pvbus_doattach, sc, &lwp, "%sattach", device_xname(self))) {
		aprint_error_dev(self, "couldn't create thread\n");
	}
}

static void
ld_pvbus_doattach(void *arg)
{
	struct ld_pvbus_softc *sc = (struct ld_pvbus_softc *)arg;
	struct ld_softc *ld = &sc->sc_ld;

	ldattach(ld);
	config_pending_decr();
	kthread_exit(0);
}

static int
ld_pvbus_start(struct ld_softc *ld, struct buf *bp)
{
	struct ld_pvbus_softc *sc = device_private(ld->sc_dv);
	struct ld_pvbus_task *task = &sc->sc_task;

	task->task_sc = sc;
	task->task_bp = bp;
	pvbus_task_init(&task->task, ld_pvbus_dobio, task);

#if 0
	callout_reset(&task->task_callout, hz, ld_pvbus_timeout, task);
#endif
	pvbus_task_add(sc->sc_pvbus, &task->task);

	return (0);
}

static void
ld_pvbus_dobio(void *arg)
{
	struct ld_pvbus_task *task = (struct ld_pvbus_task *)arg;
	struct ld_pvbus_softc *sc = task->task_sc;
	struct ld_softc *ld = &sc->sc_ld;
	struct buf *bp = task->task_bp;
	int error, s;

#if 0
	callout_stop(&task->task_callout);
#endif

	/*
	 * I/O operation
	 */
	DPRINTF(("%s: I/O operation (dir=%s, blkno=0x%jx, bcnt=0x%x)\n",
	    device_xname(sc->sc_ld.sc_dv), bp->b_flags & B_READ ? "IN" : "OUT",
	    bp->b_rawblkno, bp->b_bcount));

	/* is everything done in terms of blocks? */
	if (bp->b_rawblkno >= ld->sc_secperunit) {
		/* trying to read or write past end of device */
		aprint_error_dev(sc->sc_ld.sc_dv,
		    "blkno exceeds capacity 0x%llx\n", ld->sc_secperunit);
		bp->b_error = EIO; /* XXX  */
		bp->b_resid = bp->b_bcount;
		lddone(&sc->sc_ld, bp);
		return;
	}
	if ((bp->b_bcount % ld->sc_secsize) != 0) {
		aprint_error_dev(sc->sc_ld.sc_dv,
		    "bcount(%d) is unaligned sector size(%d)\n",
		    bp->b_bcount, ld->sc_secsize);
		bp->b_error = EIO; /* XXX  */
		bp->b_resid = bp->b_bcount;
		lddone(&sc->sc_ld, bp);
		return;
	}
	if (((u_long)bp->b_data & 3) != 0) {
		aprint_error_dev(sc->sc_ld.sc_dv, "unaligned word address\n");
		bp->b_error = EIO; /* XXX  */
		bp->b_resid = bp->b_bcount;
		lddone(&sc->sc_ld, bp);
		return;
	}

#ifdef LD_PVBUS_DEBUG_DUMP
	if (!(bp->b_flags & B_READ)) {
		ld_pvbus_dump_data("ld_pvbus_dobio: write", bp->b_data,
		    bp->b_bcount);
	}
#endif

	s = splbio();
	if (bp->b_flags & B_READ)
		error = ld_pvbus_read_block(sc, bp->b_rawblkno, bp->b_data,
		    bp->b_bcount);
	else
		error = ld_pvbus_write_block(sc, bp->b_rawblkno, bp->b_data,
		    bp->b_bcount);
	if (error) {
		DPRINTF(("%s: error %d\n", device_xname(sc->sc_ld.sc_dv),
		    error));
		bp->b_error = EIO;	/* XXXX */
		bp->b_resid = bp->b_bcount;
	} else {
		bp->b_resid = 0;
	}
	splx(s);

#ifdef LD_PVBUS_DEBUG_DUMP
	if (error == 0 && (bp->b_flags & B_READ)) {
		ld_pvbus_dump_data("ld_pvbus_dobio: read", bp->b_data,
		    bp->b_bcount);
	}
#endif
	lddone(&sc->sc_ld, bp);
}

#if 0
static void
ld_pvbus_timeout(void *arg)
{
	struct ld_pvbus_task *task = (struct ld_pvbus_task *)arg;
	struct ld_pvbus_softc *sc = task->task_sc;
	struct buf *bp = task->task_bp;
	int s;

	s = splbio();
	if (!pvbus_task_pending(&task->task)) {
		splx(s);
		return;
	}
	bp->b_error = EIO;	/* XXX */
	bp->b_resid = bp->b_bcount;
	pvbus_task_del(&task->task);
	splx(s);

	aprint_error_dev(sc->sc_ld.sc_dv, "device timeout\n");
	lddone(&sc->sc_ld, bp);
}
#endif

static int
ld_pvbus_dump(struct ld_softc *ld, void *data, int blkno, int blkcnt)
{
	struct ld_pvbus_softc *sc = device_private(ld->sc_dv);

	return ld_pvbus_write_block(sc, blkno, data, blkcnt * ld->sc_secsize);
}

static int
ld_pvbus_read_block(struct ld_pvbus_softc *sc, uint32_t blkno, void *data,
    int count)
{
	struct ld_softc *ld = &sc->sc_ld;
	uint32_t *p = data;
	int secsize = ld->sc_secsize / 4;
	int blkoff, blkcnt, bufoff;

	blkcnt = count / ld->sc_secsize;
	for (blkoff = 0; blkoff < blkcnt; blkoff++) {
		/* buffer <- disk */
		uarm_hvcall2(HV_DEVICE_OP_READ, blkno + blkoff, HV_OP_DEVICE);

		/* read from buffer */
		for (bufoff = 0; bufoff < secsize; bufoff++) {
			*p++ = uarm_hvcall3(0, bufoff,
			    HV_BUFFER_OP_READ, HV_OP_BUFFER);
		}
	}

	return (0);
}

static int
ld_pvbus_write_block(struct ld_pvbus_softc *sc, uint32_t blkno, void *data,
    int count)
{
	struct ld_softc *ld = &sc->sc_ld;
	uint32_t *p = data;
	int secsize = ld->sc_secsize / 4;
	int blkoff, blkcnt, bufoff;

	blkcnt = count / ld->sc_secsize;
	for (blkoff = 0; blkoff < blkcnt; blkoff++) {
		/* write to buffer */
		for (bufoff = 0; bufoff < secsize; bufoff++) {
			(void) uarm_hvcall3(*p++, bufoff,
			    HV_BUFFER_OP_WRITE, HV_OP_BUFFER);
		}

		/* buffer -> disk */
		uarm_hvcall2(HV_DEVICE_OP_WRITE, blkno + blkoff, HV_OP_DEVICE);
	}

	return (0);
}

#ifdef LD_PVBUS_DEBUG_DUMP
static void
ld_pvbus_dump_data(const char *title, const void *ptr, size_t size)
{
	char buf[80];
	char cbuf[16];
	char t[4];
	const uint8_t *p = ptr;
	size_t i, j;

	printf("%s: %s\n", __func__, (title != NULL) ? title : "");
	printf("--------+--------------------------------------------------+------------------+\n");
	printf("offset  | +0 +1 +2 +3 +4 +5 +6 +7  +8 +9 +a +b +c +d +e +f | data             |\n");
	printf("--------+--------------------------------------------------+------------------+\n");
	for (i = 0; i < size; i++) {
		if ((i % 16) == 0) {
			snprintf(buf, sizeof(buf), "%08zx| ", i);
		} else if ((i % 16) == 8) {
			strlcat(buf, " ", sizeof(buf));
		}

		snprintf(t, sizeof(t), "%02x ", p[i]);
		strlcat(buf, t, sizeof(buf));
		cbuf[i % 16] = p[i];

		if ((i % 16) == 15) {
			strlcat(buf, "| ", sizeof(buf));
			for (j = 0; j < 16; j++) {
				if (cbuf[j] >= 0x20 && cbuf[j] <= 0x7e) {
					snprintf(t, sizeof(t), "%c", cbuf[j]);
					strlcat(buf, t, sizeof(buf));
				} else {
					strlcat(buf, ".", sizeof(buf));
				}
			}
			printf("%s |\n", buf);
		}
	}
	j = i % 16;
	if (j != 0) {
		for (; j < 16; j++) {
			strlcat(buf, "   ", sizeof(buf));
			if ((j % 16) == 8) {
				strlcat(buf, " ", sizeof(buf));
			}
		}

		strlcat(buf, "| ", sizeof(buf));
		for (j = 0; j < (i % 16); j++) {
			if (cbuf[j] >= 0x20 && cbuf[j] <= 0x7e) {
				snprintf(t, sizeof(t), "%c", cbuf[j]);
				strlcat(buf, t, sizeof(buf));
			} else {
				strlcat(buf, ".", sizeof(buf));
			}
		}
		for (; j < 16; j++) {
			strlcat(buf, " ", sizeof(buf));
		}
		printf("%s |\n", buf);
	}
	printf("--------+--------------------------------------------------+------------------+\n");
}
#endif	/* LD_PVBUS_DEBUG_DUMP */
