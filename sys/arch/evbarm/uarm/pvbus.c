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
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/callout.h>

#include <arch/evbarm/uarm/pvbusvar.h>

struct pvbus_softc {
	device_t sc_dev;		/* base device */

	/* task queue */
	struct lwp *sc_tskq_lwp;	/* asynchronous tasks */
	TAILQ_HEAD(, pvbus_task) sc_tskq;   /* task thread work queue */
	struct kmutex sc_tskq_mtx;
	struct kcondvar sc_tskq_cv;
};

static int pvbus_match(device_t, cfdata_t, void *);
static void pvbus_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(pvbus, sizeof(struct pvbus_softc),
    pvbus_match, pvbus_attach, NULL, NULL);

static void pvbus_doattach(device_t);
static void pvbus_task_thread(void *);


/*ARGSUSED*/
static int
pvbus_match(device_t parent, cfdata_t cf, void *aux)
{

	return (1);
}

/*ARGSUSED*/
static void
pvbus_attach(device_t parent, device_t self, void *aux)
{
	struct pvbus_softc *sc = device_private(self);

	sc->sc_dev = self;

	aprint_normal("\n");
	aprint_naive("\n");

	TAILQ_INIT(&sc->sc_tskq);
	mutex_init(&sc->sc_tskq_mtx, MUTEX_DEFAULT, IPL_BIO);
	cv_init(&sc->sc_tskq_cv, "pvbustaskq");

	/*
	 * Create the event thread that will attach and detach cards
	 * and perform other lengthy operations.
	 */
	config_pending_incr();
	config_interrupts(self, pvbus_doattach);
}

static void
pvbus_doattach(device_t dev)
{
	struct pvbus_softc *sc = device_private(dev);

	if (kthread_create(PRI_NONE, KTHREAD_MPSAFE, NULL,
	    pvbus_task_thread, sc, &sc->sc_tskq_lwp, "%s", device_xname(dev))) {
		aprint_error_dev(dev, "couldn't create task thread\n");
	}
}

void
pvbus_task_init(struct pvbus_task *task, void (*func)(void *), void *arg)
{

	task->func = func;
	task->arg = arg;
	task->onqueue = 0;
	task->sc = NULL;
}

void
pvbus_task_add(struct pvbus_softc *sc, struct pvbus_task *task)
{

	mutex_enter(&sc->sc_tskq_mtx);
	task->onqueue = 1;
	task->sc = sc;
	TAILQ_INSERT_TAIL(&sc->sc_tskq, task, next);
	cv_broadcast(&sc->sc_tskq_cv);
	mutex_exit(&sc->sc_tskq_mtx);
}

static inline void
pvbus_task_del1(struct pvbus_softc *sc, struct pvbus_task *task)
{

	TAILQ_REMOVE(&sc->sc_tskq, task, next);
	task->sc = NULL;
	task->onqueue = 0;
}

void
pvbus_task_del(struct pvbus_task *task)
{
	struct pvbus_softc *sc = (struct pvbus_softc *)task->sc;

	if (sc != NULL) {
		mutex_enter(&sc->sc_tskq_mtx);
		pvbus_task_del1(sc, task);
		mutex_exit(&sc->sc_tskq_mtx);
	}
}

int
pvbus_task_pending(struct pvbus_task *task)
{

	return task->onqueue;
}

static void
pvbus_task_thread(void *arg)
{
	struct pvbus_softc *sc = (struct pvbus_softc *)arg;
	struct pvbus_task *task;

	config_found(sc->sc_dev, sc, NULL);
	config_pending_decr();

	mutex_enter(&sc->sc_tskq_mtx);
	for (;;) {
		task = TAILQ_FIRST(&sc->sc_tskq);
		if (task != NULL) {
			pvbus_task_del1(sc, task);
			mutex_exit(&sc->sc_tskq_mtx);
			(*task->func)(task->arg);
			mutex_enter(&sc->sc_tskq_mtx);
		} else {
			cv_wait(&sc->sc_tskq_cv, &sc->sc_tskq_mtx);
		}
	}
	/* time to die. */
	sc->sc_tskq_lwp = NULL;
	cv_broadcast(&sc->sc_tskq_cv);
	mutex_exit(&sc->sc_tskq_mtx);
	kthread_exit(0);
}
