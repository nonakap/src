#	$NetBSD: Makefile,v 1.3 2013/02/04 15:00:32 joerg Exp $

PROG_CXX=	clang-tblgen
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/utils/TableGen

SRCS=	ClangASTNodesEmitter.cpp \
	ClangAttrEmitter.cpp \
	ClangCommentCommandInfoEmitter.cpp \
	ClangCommentHTMLNamedCharacterReferenceEmitter.cpp \
	ClangCommentHTMLTagsEmitter.cpp \
	ClangDiagnosticsEmitter.cpp \
	ClangSACheckersEmitter.cpp \
	NeonEmitter.cpp \
	OptParserEmitter.cpp \
	TableGen.cpp

LLVM_LIBS+= \
	TableGen \
	Support

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
