# $NetBSD: Makefile,v 1.1 2013/02/16 21:29:50 jmmv Exp $

.include <bsd.own.mk>

TESTSDIR=	${TESTSBASE}/lib/liblutok

SRCDIR=		${NETBSDSRCDIR}/external/bsd/lutok/dist
.PATH:		${SRCDIR}

CPPFLAGS+=	-DHAVE_CONFIG_H
CPPFLAGS+=	-I${NETBSDSRCDIR}/lib/liblutok

FILESDIR=	${TESTSDIR}

TESTS_CXX=	c_gate_test \
		debug_test \
		exceptions_test \
		operations_test \
		stack_cleaner_test \
		state_test

LDADD+=		-llutok
DPADD+=		${LIBLUTOK}

.include <bsd.test.mk>
