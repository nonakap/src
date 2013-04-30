# $NetBSD: Makefile,v 1.1 2012/03/25 00:11:16 nonaka Exp $

.include "../Makefile.inc"

.PATH:	${S}/dev/usb

KMOD=	if_ursu
IOCONF=	if_ursu.ioconf
SRCS=	if_ursu.c

WARNS=	4

CFLAGS+= -DUSB_DEBUG

.include <bsd.kmodule.mk>
