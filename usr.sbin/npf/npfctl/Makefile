# $NetBSD: Makefile,v 1.10 2013/02/09 03:35:32 rmind Exp $

BINDIR=/sbin
PROG=		npfctl
MAN=		npfctl.8 npf.conf.5

SRCS=		npfctl.c npf_var.c npf_data.c npf_ncgen.c npf_build.c \
		npf_extmod.c npf_disassemble.c

CPPFLAGS+=	-I${.CURDIR}
SRCS+=		npf_scan.l npf_parse.y
YHEADER=	1

LDADD+=		-lnpf -lprop -lcrypto -lutil -ly
DPADD+=		${LIBNPF} ${LIBPROP} ${LIBUTIL}

WARNS=		5
NOLINT=		# disabled deliberately

.include <bsd.prog.mk>
