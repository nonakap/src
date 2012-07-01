# $NetBSD$

.include "../Makefile.inc"
.include "../../kiconv/modules/Makefile.inc"

KMOD=	kiconv_mapper_parallel
SRCS=	citrus_mapper_serial.c

CPPFLAGS+= -DKICONV_MAPPER_ONLY_PARALLEL

.include <bsd.kmodule.mk>
