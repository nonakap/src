/*	$NetBSD$	*/

/*-
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/bitops.h>

#include <machine/i82489reg.h>

#include <machine/vmm.h>
#include <amd64/vmm/vmm_lapic.h>
#include <amd64/vmm/vmm_ktr.h>
#include <amd64/vmm/io/vdev.h>
#include <amd64/vmm/io/vlapic.h>

#define	VLAPIC_CTR0(vlapic, format)					\
	VMM_CTR0((vlapic)->vm, (vlapic)->vcpuid, format)

#define	VLAPIC_CTR1(vlapic, format, p1)					\
	VMM_CTR1((vlapic)->vm, (vlapic)->vcpuid, format, p1)

#define	VLAPIC_CTR_IRR(vlapic, msg)					\
do {									\
	uint32_t *lirrptr = &(vlapic)->apic.irr0;			\
	lirrptr[0] = lirrptr[0];	/* silence compiler */		\
	VLAPIC_CTR1((vlapic), msg " irr0 0x%08x", lirrptr[0 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr1 0x%08x", lirrptr[1 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr2 0x%08x", lirrptr[2 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr3 0x%08x", lirrptr[3 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr4 0x%08x", lirrptr[4 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr5 0x%08x", lirrptr[5 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr6 0x%08x", lirrptr[6 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " irr7 0x%08x", lirrptr[7 << 2]);	\
} while (/*CONSTCOND*/0)

#define	VLAPIC_CTR_ISR(vlapic, msg)					\
do {									\
	uint32_t *lisrptr = &(vlapic)->apic.isr0;			\
	lisrptr[0] = lisrptr[0];	/* silence compiler */		\
	VLAPIC_CTR1((vlapic), msg " isr0 0x%08x", lisrptr[0 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr1 0x%08x", lisrptr[1 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr2 0x%08x", lisrptr[2 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr3 0x%08x", lisrptr[3 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr4 0x%08x", lisrptr[4 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr5 0x%08x", lisrptr[5 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr6 0x%08x", lisrptr[6 << 2]);	\
	VLAPIC_CTR1((vlapic), msg " isr7 0x%08x", lisrptr[7 << 2]);	\
} while (/*CONSTCOND*/0)

MALLOC_DEFINE(M_VLAPIC, "vlapic", "vlapic");

#define	PRIO(x)			((x) >> 4)

#define VLAPIC_VERSION		(16)
#define VLAPIC_MAXLVT_ENTRIES	(5)

struct LAPIC {
	uint32_t id;
	uint32_t version;
	uint32_t tpr;
	uint32_t apr;
	uint32_t ppr;
	uint32_t eoi;
	uint32_t ldr;
	uint32_t dfr;
	uint32_t svr;
	uint32_t isr0;
	uint32_t isr1;
	uint32_t isr2;
	uint32_t isr3;
	uint32_t isr4;
	uint32_t isr5;
	uint32_t isr6;
	uint32_t isr7;
	uint32_t tmr0;
	uint32_t tmr1;
	uint32_t tmr2;
	uint32_t tmr3;
	uint32_t tmr4;
	uint32_t tmr5;
	uint32_t tmr6;
	uint32_t tmr7;
	uint32_t irr0;
	uint32_t irr1;
	uint32_t irr2;
	uint32_t irr3;
	uint32_t irr4;
	uint32_t irr5;
	uint32_t irr6;
	uint32_t irr7;
	uint32_t esr;
	uint32_t lvt_cmci;
	uint32_t icr_lo;
	uint32_t icr_hi;
	uint32_t lvt_timer;
	uint32_t lvt_thermal;
	uint32_t lvt_pcint;
	uint32_t lvt_lint0;
	uint32_t lvt_lint1;
	uint32_t lvt_error;
	uint32_t icr_timer;
	uint32_t ccr_timer;
	uint32_t dcr_timer;
};

struct vlapic {
	struct vm		*vm;
	int			vcpuid;

	struct io_region	*mmio;
	struct vdev_ops		*ops;
	struct LAPIC		 apic;

	int			 esr_update;

	int			 divisor;
	int			 ccr_ticks;

	/*
	 * The 'isrvec_stk' is a stack of vectors injected by the local apic.
	 * A vector is popped from the stack when the processor does an EOI.
	 * The vector on the top of the stack is used to compute the
	 * Processor Priority in conjunction with the TPR.
	 */
	uint8_t			 isrvec_stk[ISRVEC_STK_SIZE];
	int			 isrvec_stk_top;
};

static __inline uint32_t
atomic_load_acq_32(volatile uint32_t *p)
{
	uint32_t tmp = *p;
	__asm __volatile("" ::: "memory");
	return (tmp);
}

static void
vlapic_mask_lvts(uint32_t *lvts, int num_lvt)
{
	int i;
	for (i = 0; i < num_lvt; i++) {
		*lvts |= LAPIC_LVTT_M;
		lvts += 4;
	}
}

#if 0
static inline void
vlapic_dump_lvt(uint32_t offset, uint32_t *lvt)
{
	printf("Offset %x: lvt %08x (V:%02x DS:%x M:%x)\n", offset,
	    *lvt, *lvt & APIC_LVTT_VECTOR, *lvt & APIC_LVTT_DS,
	    *lvt & APIC_LVTT_M);
}
#endif

static uint64_t
vlapic_get_ccr(struct vlapic *vlapic)
{
	struct LAPIC    *lapic = &vlapic->apic;
	return lapic->ccr_timer;
}

static void
vlapic_update_errors(struct vlapic *vlapic)
{
	struct LAPIC    *lapic = &vlapic->apic;
	lapic->esr = 0; // XXX 
}

static void
vlapic_init_ipi(struct vlapic *vlapic)
{
	struct LAPIC    *lapic = &vlapic->apic;
	lapic->version = VLAPIC_VERSION;
	lapic->version |= (VLAPIC_MAXLVT_ENTRIES < LAPIC_VERSION_LVT_SHIFT);
	lapic->dfr = 0xffffffff;
	lapic->svr = LAPIC_SVR_VECTOR_MASK;
	vlapic_mask_lvts(&lapic->lvt_timer, VLAPIC_MAXLVT_ENTRIES+1);
}

static int
vlapic_op_reset(void* dev)
{
	struct vlapic 	*vlapic = (struct vlapic*)dev;
	struct LAPIC	*lapic = &vlapic->apic;

	memset(lapic, 0, sizeof(*lapic));
	lapic->id = vlapic->vcpuid << 24;
	lapic->apr = vlapic->vcpuid;
	vlapic_init_ipi(vlapic);
	
	return 0;

}

static int
vlapic_op_init(void* dev)
{
	struct vlapic *vlapic = (struct vlapic*)dev;
	vdev_register_region(vlapic->ops, vlapic, vlapic->mmio);
	return vlapic_op_reset(dev);
}

static int
vlapic_op_halt(void* dev)
{
	struct vlapic *vlapic = (struct vlapic*)dev;
	vdev_unregister_region(vlapic, vlapic->mmio);
	return 0;

}

void
vlapic_set_intr_ready(struct vlapic *vlapic, int vector)
{
	struct LAPIC	*lapic = &vlapic->apic;
	uint32_t	*irrptr;
	int		idx;

	if (vector < 0 || vector >= 256)
		panic("vlapic_set_intr_ready: invalid vector %d\n", vector);

	idx = (vector / 32) * 4;
	irrptr = &lapic->irr0;
	atomic_or_32(&irrptr[idx], 1 << (vector % 32));
	VLAPIC_CTR_IRR(vlapic, "vlapic_set_intr_ready");
}

extern uint64_t tsc_freq;
#define VLAPIC_BUS_FREQ	tsc_freq
#define VLAPIC_DCR(x)	((x->dcr_timer & 0x8) >> 1)|(x->dcr_timer & 0x3)

static int
vlapic_timer_divisor(uint32_t dcr)
{

	switch (dcr & 0xb) {
	case LAPIC_DCRT_DIV2:
		return (2);
	case LAPIC_DCRT_DIV4:
		return (4);
	case LAPIC_DCRT_DIV8:
		return (8);
	case LAPIC_DCRT_DIV16:
		return (16);
	case LAPIC_DCRT_DIV32:
		return (32);
	case LAPIC_DCRT_DIV64:
		return (64);
	case LAPIC_DCRT_DIV128:
		return (128);
	default:
		panic("vlapic_timer_divisor: invalid dcr 0x%08x", dcr);
	}
}

static void
vlapic_start_timer(struct vlapic *vlapic, uint32_t elapsed)
{
	uint32_t icr_timer;

	icr_timer = vlapic->apic.icr_timer;

	vlapic->ccr_ticks = hardclock_ticks;
	if (elapsed < icr_timer)
		vlapic->apic.ccr_timer = icr_timer - elapsed;
	else {
		/*
		 * This can happen when the guest is trying to run its local
		 * apic timer higher that the setting of 'hz' in the host.
		 *
		 * We deal with this by running the guest local apic timer
		 * at the rate of the host's 'hz' setting.
		 */
		vlapic->apic.ccr_timer = 0;
	}
}

static __inline uint32_t *
vlapic_get_lvt(struct vlapic *vlapic, uint32_t offset)
{
	struct LAPIC	*lapic = &vlapic->apic;
	int 		 i;

	if (offset < APIC_OFFSET_TIMER_LVT || offset > APIC_OFFSET_ERROR_LVT) {
		panic("vlapic_get_lvt: invalid LVT\n");
	}
	i = (offset - APIC_OFFSET_TIMER_LVT) >> 2;
	return ((&lapic->lvt_timer) + i);;
}

#if 1
static void
dump_isrvec_stk(struct vlapic *vlapic)
{
	int i;
	uint32_t *isrptr;

	isrptr = &vlapic->apic.isr0;
	for (i = 0; i < 8; i++)
		printf("ISR%d 0x%08x\n", i, isrptr[i * 4]);

	for (i = 0; i <= vlapic->isrvec_stk_top; i++)
		printf("isrvec_stk[%d] = %d\n", i, vlapic->isrvec_stk[i]);
}
#endif

/*
 * Algorithm adopted from section "Interrupt, Task and Processor Priority"
 * in Intel Architecture Manual Vol 3a.
 */
static void
vlapic_update_ppr(struct vlapic *vlapic)
{
	int isrvec, tpr, ppr;

	/*
	 * Note that the value on the stack at index 0 is always 0.
	 *
	 * This is a placeholder for the value of ISRV when none of the
	 * bits is set in the ISRx registers.
	 */
	isrvec = vlapic->isrvec_stk[vlapic->isrvec_stk_top];
	tpr = vlapic->apic.tpr;

#if 1
	{
		int i, lastprio, curprio, vector, idx;
		uint32_t *isrptr;

		if (vlapic->isrvec_stk_top == 0 && isrvec != 0)
			panic("isrvec_stk is corrupted: %d", isrvec);

		/*
		 * Make sure that the priority of the nested interrupts is
		 * always increasing.
		 */
		lastprio = -1;
		for (i = 1; i <= vlapic->isrvec_stk_top; i++) {
			curprio = PRIO(vlapic->isrvec_stk[i]);
			if (curprio <= lastprio) {
				dump_isrvec_stk(vlapic);
				panic("isrvec_stk does not satisfy invariant");
			}
			lastprio = curprio;
		}

		/*
		 * Make sure that each bit set in the ISRx registers has a
		 * corresponding entry on the isrvec stack.
		 */
		i = 1;
		isrptr = &vlapic->apic.isr0;
		for (vector = 0; vector < 256; vector++) {
			idx = (vector / 32) * 4;
			if (isrptr[idx] & (1 << (vector % 32))) {
				if (i > vlapic->isrvec_stk_top ||
				    vlapic->isrvec_stk[i] != vector) {
					dump_isrvec_stk(vlapic);
					panic("ISR and isrvec_stk out of sync");
				}
				i++;
			}
		}
	}
#endif

	if (PRIO(tpr) >= PRIO(isrvec))
		ppr = tpr;
	else
		ppr = isrvec & 0xf0;

	vlapic->apic.ppr = ppr;
	VLAPIC_CTR1(vlapic, "vlapic_update_ppr 0x%02x", ppr);
}

static void
vlapic_process_eoi(struct vlapic *vlapic)
{
	struct LAPIC	*lapic = &vlapic->apic;
	uint32_t	*isrptr;
	int		i, idx, bitpos;

	isrptr = &lapic->isr0;

	/*
	 * The x86 architecture reserves the the first 32 vectors for use
	 * by the processor.
	 */
	for (i = 7; i > 0; i--) {
		idx = i * 4;
		bitpos = fls32(isrptr[idx]);
		if (bitpos != 0) {
			if (vlapic->isrvec_stk_top <= 0) {
				panic("invalid vlapic isrvec_stk_top %d",
				      vlapic->isrvec_stk_top);
			}
			isrptr[idx] &= ~(1 << (bitpos - 1));
			VLAPIC_CTR_ISR(vlapic, "vlapic_process_eoi");
			vlapic->isrvec_stk_top--;
			vlapic_update_ppr(vlapic);
			return;
		}
	}
}

static __inline int
vlapic_get_lvt_field(uint32_t *lvt, uint32_t mask)
{
	return (*lvt & mask);
}

static __inline int
vlapic_periodic_timer(struct vlapic *vlapic)
{
	uint32_t *lvt;
	
	lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_TIMER_LVT);

	return (vlapic_get_lvt_field(lvt, LAPIC_LVTT_TM_PERIODIC));
}

static void
vlapic_fire_timer(struct vlapic *vlapic)
{
	int vector;
	uint32_t *lvt;
	
	lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_TIMER_LVT);

	if (!vlapic_get_lvt_field(lvt, LAPIC_LVTT_M)) {
		vector = vlapic_get_lvt_field(lvt, LAPIC_LVTT_VEC_MASK);
		vlapic_set_intr_ready(vlapic, vector);
	}
}

static int
lapic_process_icr(struct vlapic *vlapic, uint64_t icrval)
{
	kcpuset_t *dmask;
	uint32_t dest, vec, mode;
	int i;

	dest = icrval >> 32;
	vec = icrval & LAPIC_ICRLO_VEC_MASK;
	mode = icrval & LAPIC_DLMODE_MASK;

	if (mode == LAPIC_DLMODE_FIXED || mode == LAPIC_DLMODE_NMI) {
		switch (icrval & LAPIC_DEST_MASK) {
		case LAPIC_DEST_DEFAULT:
			kcpuset_create(&dmask, true);
			kcpuset_set(dmask, dest);
			break;
		case LAPIC_DEST_SELF:
			kcpuset_create(&dmask, true);
			kcpuset_set(dmask, vlapic->vcpuid);
			break;
		case LAPIC_DEST_ALLINCL:
			dmask = vm_active_cpus(vlapic->vm);
			kcpuset_use(dmask);
			break;
		case LAPIC_DEST_ALLEXCL:
			dmask = vm_active_cpus(vlapic->vm);
			kcpuset_use(dmask);
			kcpuset_clear(dmask, vlapic->vcpuid);
			break;
		}

		for (i = 0; i < MAXCPUS; i++) {
			if (!kcpuset_isset(dmask, i))
				continue;

			kcpuset_clear(dmask, i);
			if (mode == LAPIC_DLMODE_FIXED)
				lapic_set_intr(vlapic->vm, i, vec);
			else
				vm_inject_nmi(vlapic->vm, i);
		}

		kcpuset_unuse(dmask, NULL);

		return (0);	/* handled completely in the kernel */
	}

	/*
	 * XXX this assumes that the startup IPI always succeeds
	 */
	if (mode == LAPIC_DLMODE_STARTUP)
		vm_activate_cpu(vlapic->vm, dest);

	/*
	 * This will cause a return to userland.
	 */
	return (1);
}

int
vlapic_pending_intr(struct vlapic *vlapic)
{
	struct LAPIC	*lapic = &vlapic->apic;
	int	  	 idx, i, bitpos, vector;
	uint32_t	*irrptr, val;

	irrptr = &lapic->irr0;

	/*
	 * The x86 architecture reserves the the first 32 vectors for use
	 * by the processor.
	 */
	for (i = 7; i > 0; i--) {
		idx = i * 4;
		val = atomic_load_acq_32(&irrptr[idx]);
		bitpos = fls32(val);
		if (bitpos != 0) {
			vector = i * 32 + (bitpos - 1);
			if (PRIO(vector) > PRIO(lapic->ppr)) {
				VLAPIC_CTR1(vlapic, "pending intr %d", vector);
				return (vector);
			} else 
				break;
		}
	}
	VLAPIC_CTR0(vlapic, "no pending intr");
	return (-1);
}

void
vlapic_intr_accepted(struct vlapic *vlapic, int vector)
{
	struct LAPIC	*lapic = &vlapic->apic;
	uint32_t	*irrptr, *isrptr;
	int		idx, stk_top;

	/*
	 * clear the ready bit for vector being accepted in irr 
	 * and set the vector as in service in isr.
	 */
	idx = (vector / 32) * 4;

	irrptr = &lapic->irr0;
	atomic_and_32(&irrptr[idx], ~(1 << (vector % 32)));
	VLAPIC_CTR_IRR(vlapic, "vlapic_intr_accepted");

	isrptr = &lapic->isr0;
	isrptr[idx] |= 1 << (vector % 32);
	VLAPIC_CTR_ISR(vlapic, "vlapic_intr_accepted");

	/*
	 * Update the PPR
	 */
	vlapic->isrvec_stk_top++;

	stk_top = vlapic->isrvec_stk_top;
	if (stk_top >= ISRVEC_STK_SIZE)
		panic("isrvec_stk_top overflow %d", stk_top);

	vlapic->isrvec_stk[stk_top] = vector;
	vlapic_update_ppr(vlapic);
}

int
vlapic_op_mem_read(void* dev, uint64_t gpa, opsize_t size, uint64_t *data)
{
	struct vlapic 	*vlapic = (struct vlapic*)dev;
	struct LAPIC	*lapic = &vlapic->apic;
	uint64_t	 offset = gpa & ~(PAGE_SIZE);
	uint32_t	*reg;
	int		 i;

	if (offset > sizeof(*lapic)) {
		*data = 0;
		return 0;
	}
	
	offset &= ~3;
	switch(offset)
	{
		case APIC_OFFSET_ID:
			*data = lapic->id;
			break;
		case APIC_OFFSET_VER:
			*data = lapic->version;
			break;
		case APIC_OFFSET_TPR:
			*data = lapic->tpr;
			break;
		case APIC_OFFSET_APR:
			*data = lapic->apr;
			break;
		case APIC_OFFSET_PPR:
			*data = lapic->ppr;
			break;
		case APIC_OFFSET_EOI:
			*data = lapic->eoi;
			break;
		case APIC_OFFSET_LDR:
			*data = lapic->ldr;
			break;
		case APIC_OFFSET_DFR:
			*data = lapic->dfr;
			break;
		case APIC_OFFSET_SVR:
			*data = lapic->svr;
			break;
		case APIC_OFFSET_ISR0 ... APIC_OFFSET_ISR7:
			i = (offset - APIC_OFFSET_ISR0) >> 2;
			reg = &lapic->isr0;
			*data = *(reg + i);
			break;
		case APIC_OFFSET_TMR0 ... APIC_OFFSET_TMR7:
			i = (offset - APIC_OFFSET_TMR0) >> 2;
			reg = &lapic->tmr0;
			*data = *(reg + i);
			break;
		case APIC_OFFSET_IRR0 ... APIC_OFFSET_IRR7:
			i = (offset - APIC_OFFSET_IRR0) >> 2;
			reg = &lapic->irr0;
			*data = atomic_load_acq_32(reg + i);
			break;
		case APIC_OFFSET_ESR:
			*data = lapic->esr;
			break;
		case APIC_OFFSET_ICR_LOW: 
			*data = lapic->icr_lo;
			break;
		case APIC_OFFSET_ICR_HI: 
			*data = lapic->icr_hi;
			break;
		case APIC_OFFSET_TIMER_LVT ... APIC_OFFSET_ERROR_LVT:
			reg = vlapic_get_lvt(vlapic, offset);	
			*data = *(reg);
			break;
		case APIC_OFFSET_ICR:
			*data = lapic->icr_timer;
			break;
		case APIC_OFFSET_CCR:
			*data = vlapic_get_ccr(vlapic);
			break;
		case APIC_OFFSET_DCR:
			*data = lapic->dcr_timer;
			break;
		case APIC_OFFSET_RRR:
		default:
			*data = 0;
			break;
	}
	return 0;
}

int
vlapic_op_mem_write(void* dev, uint64_t gpa, opsize_t size, uint64_t data)
{
	struct vlapic 	*vlapic = (struct vlapic*)dev;
	struct LAPIC	*lapic = &vlapic->apic;
	uint64_t	 offset = gpa & ~(PAGE_SIZE);
	uint32_t	*reg;
	int		retval;

	if (offset > sizeof(*lapic)) {
		return 0;
	}

	retval = 0;
	offset &= ~3;
	switch(offset)
	{
		case APIC_OFFSET_ID:
			lapic->id = data;
			break;
		case APIC_OFFSET_TPR:
			lapic->tpr = data & 0xff;
			vlapic_update_ppr(vlapic);
			break;
		case APIC_OFFSET_EOI:
			vlapic_process_eoi(vlapic);
			break;
		case APIC_OFFSET_LDR:
			break;
		case APIC_OFFSET_DFR:
			break;
		case APIC_OFFSET_SVR:
			lapic->svr = data;
			break;
		case APIC_OFFSET_ICR_LOW: 
			retval = lapic_process_icr(vlapic, data);
			break;
		case APIC_OFFSET_TIMER_LVT ... APIC_OFFSET_ERROR_LVT:
			reg = vlapic_get_lvt(vlapic, offset);	
			if (!(lapic->svr & LAPIC_SVR_ENABLE)) {
				data |= LAPIC_LVTT_M;
			}
			*reg = data;
			// vlapic_dump_lvt(offset, reg);
			break;
		case APIC_OFFSET_ICR:
			lapic->icr_timer = data;
			vlapic_start_timer(vlapic, 0);
			break;

		case APIC_OFFSET_DCR:
			lapic->dcr_timer = data;
			vlapic->divisor = vlapic_timer_divisor(data);
			break;

		case APIC_OFFSET_ESR:
			vlapic_update_errors(vlapic);
			break;
		case APIC_OFFSET_VER:
		case APIC_OFFSET_APR:
		case APIC_OFFSET_PPR:
		case APIC_OFFSET_RRR:
		case APIC_OFFSET_ISR0 ... APIC_OFFSET_ISR7:
		case APIC_OFFSET_TMR0 ... APIC_OFFSET_TMR7:
		case APIC_OFFSET_IRR0 ... APIC_OFFSET_IRR7:
		case APIC_OFFSET_CCR:
		default:
			// Read only.
			break;
	}

	return (retval);
}

void
vlapic_timer_tick(struct vlapic *vlapic)
{
	int curticks, delta, periodic;
	uint32_t ccr;
	uint32_t decrement, remainder;

	curticks = hardclock_ticks;

	/* Common case */
	delta = curticks - vlapic->ccr_ticks;
	if (delta == 0)
		return;

	/* Local APIC timer is disabled */
	if (vlapic->apic.icr_timer == 0)
		return;

	/* One-shot mode and timer has already counted down to zero */
	periodic = vlapic_periodic_timer(vlapic);
	if (!periodic && vlapic->apic.ccr_timer == 0)
		return;
	/*
	 * The 'curticks' and 'ccr_ticks' are out of sync by more than
	 * 2^31 ticks. We deal with this by restarting the timer.
	 */
	if (delta < 0) {
		vlapic_start_timer(vlapic, 0);
		return;
	}

	ccr = vlapic->apic.ccr_timer;
	decrement = (VLAPIC_BUS_FREQ / vlapic->divisor) / hz;
	while (delta-- > 0) {
		if (ccr <= decrement) {
			remainder = decrement - ccr;
			vlapic_fire_timer(vlapic);
			if (periodic) {
				vlapic_start_timer(vlapic, remainder);
				ccr = vlapic->apic.ccr_timer;
			} else {
				/*
				 * One-shot timer has counted down to zero.
				 */
				ccr = 0;
				break;
			}
		} else 
			ccr -= decrement;
	}

	vlapic->ccr_ticks = curticks;
	vlapic->apic.ccr_timer = ccr;
}

struct vdev_ops vlapic_dev_ops = {
	.name = "vlapic",
	.init = vlapic_op_init,
	.reset = vlapic_op_reset,
	.halt = vlapic_op_halt,
	.memread = vlapic_op_mem_read,
	.memwrite = vlapic_op_mem_write,
};
static struct io_region vlapic_mmio[VM_MAXCPU];

struct vlapic *
vlapic_init(struct vm *vm, int vcpuid)
{
	struct vlapic 		*vlapic;

	vlapic = malloc(sizeof(struct vlapic), M_VLAPIC, M_WAITOK | M_ZERO);
	vlapic->vm = vm;
	vlapic->vcpuid = vcpuid;
	vlapic->ops = &vlapic_dev_ops;

	vlapic->mmio = vlapic_mmio + vcpuid;
	vlapic->mmio->base = LAPIC_BASE;
	vlapic->mmio->len = PAGE_SIZE;
	vlapic->mmio->attr = MMIO_READ|MMIO_WRITE;
	vlapic->mmio->vcpu = vcpuid;

	vdev_register(&vlapic_dev_ops, vlapic);

	vlapic_op_init(vlapic);

	return (vlapic);
}

void
vlapic_cleanup(struct vlapic *vlapic)
{
	vlapic_op_halt(vlapic);
	vdev_unregister(vlapic);
	free(vlapic, M_VLAPIC);
}
