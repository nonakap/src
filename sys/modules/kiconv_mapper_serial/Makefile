# $NetBSD$

.include "../Makefile.inc"
.include "../../kiconv/modules/Makefile.inc"

KMOD=	kiconv_mapper_serial
SRCS=	citrus_mapper_serial.c

CPPFLAGS+= -DKICONV_MAPPER_ONLY_SERIAL

.include <bsd.kmodule.mk>
