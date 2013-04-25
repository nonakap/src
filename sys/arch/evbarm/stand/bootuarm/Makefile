# $NetBSD$

S=		${.CURDIR}/../../../..

PROG=		bootuarm
SRCS=		bootuarm.S

.include <bsd.own.mk>

CLEANFILES+=	${PROG}.elf
CFLAGS+=	-Wall -Wno-main -ffreestanding
CPPFLAGS+=	-D_STANDALONE
CPPFLAGS+=	-nostdinc -I. -I${.OBJDIR} -I${S} -I${S}/arch
CPPFLAGS+=	-mabi=apcs-gnu -mfloat-abi=soft -mcpu=xscale
DBG=		

LIBCRT0=	# nothing
LIBCRTBEGIN=	# nothing
LIBCRTEND=	# nothing
LIBC=		# nothing

MAN=		# no manual page
NOMAN=		# defined
STRIPFLAG=
BINMODE=	444

RELOC=		0xa0000000
ENTRY=		start

.if !make(obj) && !make(clean) && !make(cleandir)
.BEGIN:
	@[ -h machine ] || ln -s ${S}/arch/${MACHINE}/include machine
	@[ -h arm ] || ln -s ${S}/arch/${MACHINE_ARCH}/include ${MACHINE_ARCH}
.NOPATH: machine arm
.endif
CLEANFILES+= machine arm

${PROG}: ${OBJS}
	${_MKTARGET_LINK}
	${LD} -Ttext ${RELOC} -Bstatic -e ${ENTRY} -o ${.TARGET}.elf ${OBJS}
	${OBJCOPY} -S -O binary ${.TARGET}.elf ${.TARGET}
	-(cat ${.TARGET} /usr/src/sys/arch/evbarm/compile/obj.evbarm/UARM/netbsd.bin; dd if=/dev/zero bs=512 count=1) > ${.OBJDIR}/nbsdboot.tmp 2> /dev/null
	-/bin/ls -l ${.OBJDIR}/nbsdboot.tmp
	-(cat ${.OBJDIR}/nbsdboot.tmp ; dd if=/dev/zero bs=`expr 10 \* 1024 \* 1024 - \`/bin/ls -l ${.OBJDIR}/nbsdboot.tmp | awk '{print $$5}'\`` count=1) > ${.OBJDIR}/nbsdboot 2> /dev/null
	-(cat ${.TARGET} /usr/src/distrib/evbarm/instkernel/instkernel/obj.evbarm/netbsd-UARM_INSTALL.bin; dd if=/dev/zero bs=512 count=1) > ${.OBJDIR}/nbsdboot-install.tmp 2> /dev/null
	-/bin/ls -l ${.OBJDIR}/nbsdboot-install.tmp
	-(cat ${.OBJDIR}/nbsdboot-install.tmp ; dd if=/dev/zero bs=`expr 10 \* 1024 \* 1024 - \`/bin/ls -l ${.OBJDIR}/nbsdboot-install.tmp | awk '{print $$5}'\`` count=1) > ${.OBJDIR}/nbsdboot-install 2> /dev/null

image::
	-(cat ${.OBJDIR}/nbsdboot-install ; dd if=/dev/zero bs=1m count=2038) > ${.OBJDIR}/uARM-fresh.fs

remove::
	-/bin/rm -f ${.OBJDIR}/${PROG}

regen:: remove ${PROG} image
	-/bin/mv -f ${.OBJDIR}/uARM-fresh.fs ${.CURDIR}/uARM.fs

CLEANFILES+=	nbsdboot # XXX
CLEANFILES+=	nbsdboot-install nbsdboot-install.tmp # XXX
CLEANFILES+=	uARM-fresh.fs

.include <bsd.prog.mk>
