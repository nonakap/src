# $NetBSD$

.include "../Makefile.inc"
.include "../../kiconv/Makefile.inc"

KMOD=	kiconv
SRCS=	kiconv.c \
	citrus_bcs.c \
	citrus_bcs_strtol.c \
	citrus_bcs_strtoul.c \
	citrus_csmapper.c \
	citrus_ctype.c \
	citrus_ctype_fallback.c \
	citrus_db.c \
	citrus_db_hash.c \
	citrus_esdb.c \
	citrus_hash.c \
	citrus_iconv.c \
	citrus_lc_ctype.c \
	citrus_lookup.c \
	citrus_mapper.c \
	citrus_memstream.c \
	citrus_mmap.c \
	citrus_module.c \
	citrus_none.c \
	citrus_prop.c \
	citrus_stdenc.c \
	\
	ctype_.c \
	locale.c \
	rune.c \
	runetable.c \
	tolower_.c \
	toupper_.c \
	_wctrans.c \
	_wctype.c

CPPFLAGS.citrus_bcs_strtol.c+= -I${NETBSDSRCDIR}/common/lib/libc/stdlib
CPPFLAGS.citrus_bcs_strtoul.c+= -I${NETBSDSRCDIR}/common/lib/libc/stdlib

.include <bsd.kmodule.mk>
