# $NetBSD: Makefile.inc,v 1.1 2012/09/16 13:47:42 rmind Exp $

WARNS=		5
MKLINT=		no

.if exists(${.CURDIR}/../../Makefile.inc)
.include "${.CURDIR}/../../Makefile.inc"
.endif
