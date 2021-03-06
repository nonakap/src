/*	$NetBSD: t_fstatat.c,v 1.1 2012/11/18 17:41:54 manu Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_fstatat.c,v 1.1 2012/11/18 17:41:54 manu Exp $");

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

#define DIR "dir"
#define FILE "dir/fstatat"
#define BASEFILE "fstatat"
#define LINK "dir/symlink"
#define BASELINK "symlink"
#define FILEERR "dir/symlink"

ATF_TC_WITH_CLEANUP(fstatat_fd);
ATF_TC_HEAD(fstatat_fd, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fstatat works with fd");
}

ATF_TC_BODY(fstatat_fd, tc)
{
	int dfd;
	int fd;
	struct stat st1, st2;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(fstatat(dfd, BASEFILE, &st1, 0) == 0);
	ATF_REQUIRE(close(dfd) == 0);

	ATF_REQUIRE(stat(FILE, &st2) == 0);
	ATF_REQUIRE(memcmp(&st1, &st2, sizeof(st1)) == 0);
}

ATF_TC_CLEANUP(fstatat_fd, tc)
{
	(void)unlink(FILE);
	(void)unlink(FILEERR);
	(void)rmdir(DIR);
}

ATF_TC_WITH_CLEANUP(fstatat_fdcwd);
ATF_TC_HEAD(fstatat_fdcwd, tc)
{
	atf_tc_set_md_var(tc, "descr", 
			  "See that fstatat works with fd as AT_FDCWD");
}

ATF_TC_BODY(fstatat_fdcwd, tc)
{
	int fd;
	struct stat st;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE(chdir(DIR) == 0);
	ATF_REQUIRE(fstatat(AT_FDCWD, BASEFILE, &st, 0) == 0);
}

ATF_TC_CLEANUP(fstatat_fdcwd, tc)
{
	(void)unlink(FILE);
	(void)unlink(FILEERR);
	(void)rmdir(DIR);
}

ATF_TC_WITH_CLEANUP(fstatat_fdcwderr);
ATF_TC_HEAD(fstatat_fdcwderr, tc)
{
	atf_tc_set_md_var(tc, "descr", 
		  "See that fstatat fails with fd as AT_FDCWD and bad path");
}

ATF_TC_BODY(fstatat_fdcwderr, tc)
{
	struct stat st;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE(fstatat(AT_FDCWD, FILEERR, &st, 0) == -1);
}

ATF_TC_CLEANUP(fstatat_fdcwderr, tc)
{
	(void)unlink(FILE);
	(void)unlink(FILEERR);
	(void)rmdir(DIR);
}

ATF_TC_WITH_CLEANUP(fstatat_fderr1);
ATF_TC_HEAD(fstatat_fderr1, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fstatat fail with bad path");
}

ATF_TC_BODY(fstatat_fderr1, tc)
{
	int dfd;
	struct stat st;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);
	ATF_REQUIRE(fstatat(dfd, FILEERR, &st, 0) == -1);
	ATF_REQUIRE(close(dfd) == 0);
	
}

ATF_TC_CLEANUP(fstatat_fderr1, tc)
{
	(void)unlink(FILE);
	(void)unlink(FILEERR);
	(void)rmdir(DIR);
}

ATF_TC_WITH_CLEANUP(fstatat_fderr2);
ATF_TC_HEAD(fstatat_fderr2, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fstatat fails with bad fdat");
}

ATF_TC_BODY(fstatat_fderr2, tc)
{
	int dfd;
	int fd;
	char cwd[MAXPATHLEN];
	struct stat st;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE((dfd = open(getcwd(cwd, MAXPATHLEN), O_RDONLY, 0)) != -1);
	ATF_REQUIRE(fstatat(dfd, BASEFILE, &st, 0) == -1);
	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC_CLEANUP(fstatat_fderr2, tc)
{
	(void)unlink(FILE);
	(void)unlink(FILEERR);
	(void)rmdir(DIR);
}

ATF_TC_WITH_CLEANUP(fstatat_fderr3);
ATF_TC_HEAD(fstatat_fderr3, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fstatat fails with fd as -1");
}

ATF_TC_BODY(fstatat_fderr3, tc)
{
	int fd;
	struct stat st;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE((fd = open(FILE, O_CREAT|O_RDWR, 0644)) != -1);
	ATF_REQUIRE(close(fd) == 0);

	ATF_REQUIRE(fstatat(-1, FILE, &st, 0) == -1);
}

ATF_TC_CLEANUP(fstatat_fderr3, tc)
{
	(void)unlink(FILE);
	(void)unlink(FILEERR);
	(void)rmdir(DIR);
}


ATF_TC_WITH_CLEANUP(fstatat_fdlink);
ATF_TC_HEAD(fstatat_fdlink, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fstatat works on symlink");
}

ATF_TC_BODY(fstatat_fdlink, tc)
{
	int dfd;
	struct stat st;

	ATF_REQUIRE(mkdir(DIR, 0755) == 0);
	ATF_REQUIRE(symlink(FILE, LINK) == 0); /* target does not exists */

	ATF_REQUIRE((dfd = open(DIR, O_RDONLY, 0)) != -1);

	ATF_REQUIRE(fstatat(dfd, BASELINK, &st, 0) == -1);
	ATF_REQUIRE(errno == ENOENT);

	ATF_REQUIRE(fstatat(dfd, BASELINK, &st, AT_SYMLINK_NOFOLLOW) == 0);

	ATF_REQUIRE(close(dfd) == 0);
}

ATF_TC_CLEANUP(fstatat_fdlink, tc)
{
	(void)unlink(LINK);
	(void)rmdir(DIR);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fstatat_fd);
	ATF_TP_ADD_TC(tp, fstatat_fdcwd);
	ATF_TP_ADD_TC(tp, fstatat_fdcwderr);
	ATF_TP_ADD_TC(tp, fstatat_fderr1);
	ATF_TP_ADD_TC(tp, fstatat_fderr2);
	ATF_TP_ADD_TC(tp, fstatat_fderr3);
	ATF_TP_ADD_TC(tp, fstatat_fdlink);

	return atf_no_error();
}
