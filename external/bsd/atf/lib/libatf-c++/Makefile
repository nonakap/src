# $NetBSD: Makefile,v 1.18 2013/02/19 15:35:43 jmmv Exp $

NOLINT=		# defined

.include <bsd.own.mk>

LIB=		atf-c++
LIBISCXX=	yes

LIBDPLIBS+=     atf-c	${.CURDIR}/../libatf-c
.if ${HAVE_GCC} == 4
LIBDPLIBS+=     stdc++	${.CURDIR}/../../../../../gnu/lib/libstdc++-v3_4
.else
LIBDPLIBS+=     stdc++	${.CURDIR}/../../../../../external/gpl3/gcc/lib/libstdc++-v3
.endif
LIBDPLIBS+=     m	${.CURDIR}/../../../../../lib/libm


SRCDIR=		${NETBSDSRCDIR}/external/bsd/atf/dist
.PATH:		${SRCDIR}
.PATH:		${SRCDIR}/atf-c++
.PATH:		${SRCDIR}/atf-c++/detail

CPPFLAGS+=	-I${.CURDIR}/../libatf-c
CPPFLAGS+=	-I.

CPPFLAGS+=	-DHAVE_CONFIG_H

WARNS?=		2

SRCS=		application.cpp \
		build.cpp \
		check.cpp \
		config.cpp \
		env.cpp \
		exceptions.cpp \
		expand.cpp \
		fs.cpp \
		parser.cpp \
		process.cpp \
		tests.cpp \
		text.cpp \
		ui.cpp \
		utils.cpp

INCS=		build.hpp \
		check.hpp \
		config.hpp \
		macros.hpp \
		noncopyable.hpp \
		tests.hpp \
		utils.hpp
INCSDIR=	/usr/include/atf-c++

INCS+=		atf-c++.hpp
INCSDIR_atf-c++.hpp=/usr/include

MAN=		atf-c++-api.3

.if ${MKSHARE} != "no"
FILES+=		atf-c++.pc
FILESDIR=	/usr/lib/pkgconfig

realall: atf-c++.pc
atf-c++.pc: Makefile atf-c++.pc.in
	${TOOL_SED} -e 's,__ATF_VERSION__,0.17,g' \
	    -e 's,__CXX__,g++,g' \
	    -e 's,__INCLUDEDIR__,/usr/include,g' \
	    -e 's,__LIBDIR__,/usr/lib,g' \
	    <${SRCDIR}/atf-c++/atf-c++.pc.in >atf-c++.pc
CLEANFILES+=	atf-c++.pc
.endif

.include <bsd.lib.mk>
