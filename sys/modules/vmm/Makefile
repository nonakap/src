#	$NetBSD$

.include "../Makefile.inc"

.PATH:	${S}/arch/amd64/vmm ${S}/arch/amd64/vmm/io ${S}/arch/amd64/vmm/intel

KMOD=	vmm	
IOCONF=	vmm.ioconf
SRCS=	vmm.c
SRCS+=	vmm.c
SRCS+=	vmm_dev.c
SRCS+=	vmm_ipi.c
SRCS+=	vmm_lapic.c
SRCS+=	vmm_mem.c
SRCS+=	vmm_msr.c
SRCS+=	vmm_stat.c
SRCS+=	vmm_util.c
SRCS+=	x86.c
SRCS+=	vmm_support.S

# vmm/io
SRCS+=	vdev.c
SRCS+=	vlapic.c

# vmm/intel
SRCS+=	ept.c
SRCS+=	vmcs.c
SRCS+=	vmx_msr.c
SRCS+=	vmx.c

WARNS=	4

.include <bsd.kmodule.mk>
