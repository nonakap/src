/*	$NetBSD: msdosfs_conv.c,v 1.9 2013/01/26 16:51:51 christos Exp $	*/

/*-
 * Copyright (C) 1995, 1997 Wolfgang Solfrank.
 * Copyright (C) 1995, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com) (see below).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: msdosfs_conv.c,v 1.9 2013/01/26 16:51:51 christos Exp $");

/*
 * System include files.
 */
#include <sys/param.h>
#include <sys/time.h>
#ifdef _KERNEL
#include <sys/dirent.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#if !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
#include <sys/kiconv.h>
#else
#include <stdio.h>
#include <dirent.h>
#include <sys/queue.h>
#endif

/*
 * MSDOSFS include files.
 */
#include <fs/msdosfs/bpb.h>
#include <fs/msdosfs/direntry.h>
#include <fs/msdosfs/denode.h>
#include <fs/msdosfs/msdosfsmount.h>

struct msdosfs_winfn {
	const u_char *un;	/* unix filename */
	int unlen;		/* unix filename length */
#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
	u_char *wn;		/* UTF-16LE filename */
	u_char *lcwn;		/* lower case UTF-16LE filename */
	int wnlen;		/* win filename length */
	wchar_t *wcs;
	int wcslen;
#endif
};

/*
 * Days in each month in a regular year.
 */
u_short const regyear[] = {
	31, 28, 31, 30, 31, 30,
	31, 31, 30, 31, 30, 31
};

/*
 * Days in each month in a leap year.
 */
u_short const leapyear[] = {
	31, 29, 31, 30, 31, 30,
	31, 31, 30, 31, 30, 31
};

/*
 * Variables used to remember parts of the last time conversion.  Maybe we
 * can avoid a full conversion.
 */
u_long lasttime;
u_long lastday;
u_short lastddate;
u_short lastdtime;

/*
 * Convert the unix version of time to dos's idea of time to be used in
 * file timestamps. The passed in unix time is assumed to be in GMT.
 */
void
unix2dostime(const struct timespec *tsp, int gmtoff, u_int16_t *ddp, u_int16_t *dtp, u_int8_t *dhp)
{
	u_long t;
	u_long days;
	u_long inc;
	u_long year;
	u_long month;
	const u_short *months;

	/*
	 * If the time from the last conversion is the same as now, then
	 * skip the computations and use the saved result.
	 */
	t = tsp->tv_sec + gmtoff; /* time zone correction */
	t &= ~1;
	if (lasttime != t) {
		lasttime = t;
		lastdtime = (((t / 2) % 30) << DT_2SECONDS_SHIFT)
		    + (((t / 60) % 60) << DT_MINUTES_SHIFT)
		    + (((t / 3600) % 24) << DT_HOURS_SHIFT);

		/*
		 * If the number of days since 1970 is the same as the last
		 * time we did the computation then skip all this leap year
		 * and month stuff.
		 */
		days = t / (24 * 60 * 60);
		if (days != lastday) {
			lastday = days;
			for (year = 1970;; year++) {
				inc = year & 0x03 ? 365 : 366;
				if (days < inc)
					break;
				days -= inc;
			}
			months = year & 0x03 ? regyear : leapyear;
			for (month = 0; month < 12; month++) {
				if (days < months[month])
					break;
				days -= months[month];
			}
			lastddate = ((days + 1) << DD_DAY_SHIFT)
			    + ((month + 1) << DD_MONTH_SHIFT);
			/*
			 * Remember dos's idea of time is relative to 1980.
			 * unix's is relative to 1970.  If somehow we get a
			 * time before 1980 then don't give totally crazy
			 * results.
			 */
			if (year > 1980)
				lastddate += (year - 1980) << DD_YEAR_SHIFT;
		}
	}
	if (dtp)
		*dtp = lastdtime;
	if (dhp)
		*dhp = (tsp->tv_sec & 1) * 100 + tsp->tv_nsec / 10000000;

	*ddp = lastddate;
}

/*
 * The number of seconds between Jan 1, 1970 and Jan 1, 1980. In that
 * interval there were 8 regular years and 2 leap years.
 */
#define	SECONDSTO1980	(((8 * 365) + (2 * 366)) * (24 * 60 * 60))

u_short lastdosdate;
u_long lastseconds;

/*
 * Convert from dos' idea of time to unix'. This will probably only be
 * called from the stat(), and fstat() system calls and so probably need
 * not be too efficient.
 */
void
dos2unixtime(u_int dd, u_int dt, u_int dh, int gmtoff, struct timespec *tsp)
{
	u_long seconds;
	u_long m, month;
	u_long y, year;
	u_long days;
	const u_short *months;

	if (dd == 0) {
		/*
		 * Uninitialized field, return the epoch.
		 */
		tsp->tv_sec = 0;
		tsp->tv_nsec = 0;
		return;
	}
	seconds = ((dt & DT_2SECONDS_MASK) >> DT_2SECONDS_SHIFT) * 2
	    + ((dt & DT_MINUTES_MASK) >> DT_MINUTES_SHIFT) * 60
	    + ((dt & DT_HOURS_MASK) >> DT_HOURS_SHIFT) * 3600
	    + dh / 100;
	/*
	 * If the year, month, and day from the last conversion are the
	 * same then use the saved value.
	 */
	if (lastdosdate != dd) {
		lastdosdate = dd;
		days = 0;
		year = (dd & DD_YEAR_MASK) >> DD_YEAR_SHIFT;
		for (y = 0; y < year; y++)
			days += y & 0x03 ? 365 : 366;
		months = year & 0x03 ? regyear : leapyear;
		/*
		 * Prevent going from 0 to 0xffffffff in the following
		 * loop.
		 */
		month = (dd & DD_MONTH_MASK) >> DD_MONTH_SHIFT;
		if (month == 0) {
			printf("%s: month value out of range (%ld)\n",
			    __func__, month);
			month = 1;
		}
		for (m = 0; m < month - 1; m++)
			days += months[m];
		days += ((dd & DD_DAY_MASK) >> DD_DAY_SHIFT) - 1;
		lastseconds = (days * 24 * 60 * 60) + SECONDSTO1980;
	}
	tsp->tv_sec = seconds + lastseconds;
	tsp->tv_sec -= gmtoff;	/* time zone correction */
	tsp->tv_nsec = (dh % 100) * 10000000;
}

static const u_char
unix2dos[256] = {
	0,    0,    0,    0,    0,    0,    0,    0,	/* 00-07 */
	0,    0,    0,    0,    0,    0,    0,    0,	/* 08-0f */
	0,    0,    0,    0,    0,    0,    0,    0,	/* 10-17 */
	0,    0,    0,    0,    0,    0,    0,    0,	/* 18-1f */
	0,    '!',  0,    '#',  '$',  '%',  '&',  '\'',	/* 20-27 */
	'(',  ')',  0,    '+',  0,    '-',  0,    0,	/* 28-2f */
	'0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',	/* 30-37 */
	'8',  '9',  0,    0,    0,    0,    0,    0,	/* 38-3f */
	'@',  'A',  'B',  'C',  'D',  'E',  'F',  'G',	/* 40-47 */
	'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',	/* 48-4f */
	'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',	/* 50-57 */
	'X',  'Y',  'Z',  0,    0,    0,    '^',  '_',	/* 58-5f */
	'`',  'A',  'B',  'C',  'D',  'E',  'F',  'G',	/* 60-67 */
	'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',	/* 68-6f */
	'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',	/* 70-77 */
	'X',  'Y',  'Z',  '{',  0,    '}',  '~',  0,	/* 78-7f */
	0,    0,    0,    0,    0,    0,    0,    0,	/* 80-87 */
	0,    0,    0,    0,    0,    0,    0,    0,	/* 88-8f */
	0,    0,    0,    0,    0,    0,    0,    0,	/* 90-97 */
	0,    0,    0,    0,    0,    0,    0,    0,	/* 98-9f */
	0,    0xad, 0xbd, 0x9c, 0xcf, 0xbe, 0xdd, 0xf5,	/* a0-a7 */
	0xf9, 0xb8, 0xa6, 0xae, 0xaa, 0xf0, 0xa9, 0xee,	/* a8-af */
	0xf8, 0xf1, 0xfd, 0xfc, 0xef, 0xe6, 0xf4, 0xfa,	/* b0-b7 */
	0xf7, 0xfb, 0xa7, 0xaf, 0xac, 0xab, 0xf3, 0xa8,	/* b8-bf */
	0xb7, 0xb5, 0xb6, 0xc7, 0x8e, 0x8f, 0x92, 0x80,	/* c0-c7 */
	0xd4, 0x90, 0xd2, 0xd3, 0xde, 0xd6, 0xd7, 0xd8,	/* c8-cf */
	0xd1, 0xa5, 0xe3, 0xe0, 0xe2, 0xe5, 0x99, 0x9e,	/* d0-d7 */
	0x9d, 0xeb, 0xe9, 0xea, 0x9a, 0xed, 0xe8, 0xe1,	/* d8-df */
	0xb7, 0xb5, 0xb6, 0xc7, 0x8e, 0x8f, 0x92, 0x80,	/* e0-e7 */
	0xd4, 0x90, 0xd2, 0xd3, 0xde, 0xd6, 0xd7, 0xd8,	/* e8-ef */
	0xd1, 0xa5, 0xe3, 0xe0, 0xe2, 0xe5, 0x99, 0xf6,	/* f0-f7 */
	0x9d, 0xeb, 0xe9, 0xea, 0x9a, 0xed, 0xe8, 0x98,	/* f8-ff */
};

static const u_char
dos2unix[256] = {
	 '?',  '?',  '?',  '?',  '?',  '?',  '?',  '?',	/* 00-07 */
	 '?',  '?',  '?',  '?',  '?',  '?',  '?',  '?',	/* 08-0f */
	 '?',  '?',  '?',  '?',  '?',  '?',  '?',  '?',	/* 10-17 */
	 '?',  '?',  '?',  '?',  '?',  '?',  '?',  '?',	/* 18-1f */
	 ' ',  '!',  '"',  '#',  '$',  '%',  '&', '\'',	/* 20-27 */
	 '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',	/* 28-2f */
	 '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',	/* 30-37 */
	 '8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',	/* 38-3f */
	 '@',  'A',  'B',  'C',  'D',  'E',  'F',  'G',	/* 40-47 */
	 'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',	/* 48-4f */
	 'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',	/* 50-57 */
	 'X',  'Y',  'Z',  '[', '\\',  ']',  '^',  '_',	/* 58-5f */
	 '`',  'a',  'b',  'c',  'd',  'e',  'f',  'g',	/* 60-67 */
	 'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',	/* 68-6f */
	 'p',  'q',  'r',  's',  't',  'u',  'v',  'w',	/* 70-77 */
	 'x',  'y',  'z',  '{',  '|',  '}',  '~', 0x7f,	/* 78-7f */
	0xc7, 0xfc, 0xe9, 0xe2, 0xe4, 0xe0, 0xe5, 0xe7,	/* 80-87 */
	0xea, 0xeb, 0xe8, 0xef, 0xee, 0xec, 0xc4, 0xc5,	/* 88-8f */
	0xc9, 0xe6, 0xc6, 0xf4, 0xf6, 0xf2, 0xfb, 0xf9,	/* 90-97 */
	0xff, 0xd6, 0xdc, 0xf8, 0xa3, 0xd8, 0xd7,  '?',	/* 98-9f */
	0xe1, 0xed, 0xf3, 0xfa, 0xf1, 0xd1, 0xaa, 0xba,	/* a0-a7 */
	0xbf, 0xae, 0xac, 0xbd, 0xbc, 0xa1, 0xab, 0xbb,	/* a8-af */
	 '?',  '?',  '?',  '?',  '?', 0xc1, 0xc2, 0xc0,	/* b0-b7 */
	0xa9,  '?',  '?',  '?',  '?', 0xa2, 0xa5,  '?',	/* b8-bf */
	 '?',  '?',  '?',  '?',  '?',  '?', 0xe3, 0xc3,	/* c0-c7 */
	 '?',  '?',  '?',  '?',  '?',  '?',  '?', 0xa4,	/* c8-cf */
	0xf0, 0xd0, 0xca, 0xcb, 0xc8,  '?', 0xcd, 0xce,	/* d0-d7 */
	0xcf,  '?',  '?',  '?',  '?', 0xa6, 0xcc,  '?',	/* d8-df */
	0xd3, 0xdf, 0xd4, 0xd2, 0xf5, 0xd5, 0xb5, 0xfe,	/* e0-e7 */
	0xde, 0xda, 0xdb, 0xd9, 0xfd, 0xdd, 0xaf, 0x3f,	/* e8-ef */
	0xad, 0xb1,  '?', 0xbe, 0xb6, 0xa7, 0xf7, 0xb8,	/* f0-f7 */
	0xb0, 0xa8, 0xb7, 0xb9, 0xb3, 0xb2,  '?',  '?',	/* f8-ff */
};

static const u_char
u2l[256] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 00-07 */
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, /* 08-0f */
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, /* 10-17 */
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, /* 18-1f */
	 ' ',  '!',  '"',  '#',  '$',  '%',  '&', '\'', /* 20-27 */
	 '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/', /* 28-2f */
	 '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7', /* 30-37 */
	 '8',  '9',  ':',  ';',  '<',  '=',  '>',  '?', /* 38-3f */
	 '@',  'a',  'b',  'c',  'd',  'e',  'f',  'g', /* 40-47 */
	 'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o', /* 48-4f */
	 'p',  'q',  'r',  's',  't',  'u',  'v',  'w', /* 50-57 */
	 'x',  'y',  'z',  '[', '\\',  ']',  '^',  '_', /* 58-5f */
	 '`',  'a',  'b',  'c',  'd',  'e',  'f',  'g', /* 60-67 */
	 'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o', /* 68-6f */
	 'p',  'q',  'r',  's',  't',  'u',  'v',  'w', /* 70-77 */
	 'x',  'y',  'z',  '{',  '|',  '}',  '~', 0x7f, /* 78-7f */
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, /* 80-87 */
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, /* 88-8f */
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, /* 90-97 */
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, /* 98-9f */
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, /* a0-a7 */
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, /* a8-af */
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, /* b0-b7 */
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, /* b8-bf */
	0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, /* c0-c7 */
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, /* c8-cf */
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xd7, /* d0-d7 */
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xdf, /* d8-df */
	0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, /* e0-e7 */
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, /* e8-ef */
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, /* f0-f7 */
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, /* f8-ff */
};

/*
 * DOS filenames are made of 2 parts, the name part and the extension part.
 * The name part is 8 characters long and the extension part is 3
 * characters long.  They may contain trailing blanks if the name or
 * extension are not long enough to fill their respective fields.
 */

#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
struct msdosfs_kiconv {
	klocale_t l;	/* default locale */

	kiconv_t cih;	/* codepage -> iocharset */
	kiconv_t cdh;	/* codepage -> default encoding */
	kiconv_t dch;	/* default encoding -> codepage */
	kiconv_t idh;	/* iocharset -> default encoding */
	kiconv_t dih;	/* default encoding -> iocharset */
	kiconv_t udh;	/* UTF-16LE -> default encoding */
	kiconv_t duh;	/* default encoding -> UTF-16LE */
};

int
msdosfs_initkiconv(struct msdosfsmount *pmp)
{
	struct msdosfs_kiconv *fcp;
	int errno;

	if (pmp->pm_codepage[0] == '\0' || pmp->pm_iocharset[0] == '\0') {
		pmp->pm_kiconvcookie = NULL;
		pmp->pm_flags &= ~MSDOSFS_CONVERTFNAME;
		return 0;
	}

	fcp = malloc(sizeof(*fcp), M_MSDOSFSTMP, M_WAITOK);
	fcp->cih = fcp->cdh = fcp->dch = fcp->idh = fcp->dih =
	    fcp->udh = fcp->duh = (kiconv_t)-1;

	fcp->l = kiconv_newlocale(NULL, &errno);

	fcp->cih = kiconv_open(pmp->pm_iocharset, pmp->pm_codepage, &errno);
	if (fcp->cih == (kiconv_t)-1)
		goto error;
	fcp->cdh = kiconv_open(NULL, pmp->pm_codepage, &errno);
	if (fcp->cdh == (kiconv_t)-1)
		goto error;
	fcp->dch = kiconv_open(pmp->pm_codepage, NULL, &errno);
	if (fcp->dch == (kiconv_t)-1)
		goto error;
	fcp->dih = kiconv_open(pmp->pm_iocharset, NULL, &errno);
	if (fcp->dih == (kiconv_t)-1)
		goto error;
	fcp->idh = kiconv_open(NULL, pmp->pm_iocharset, &errno);
	if (fcp->idh == (kiconv_t)-1)
		goto error;
	fcp->udh = kiconv_open(NULL, "UTF-16LE", &errno);
	if (fcp->udh == (kiconv_t)-1)
		goto error;
	fcp->duh = kiconv_open("UTF-16LE", NULL, &errno);
	if (fcp->duh == (kiconv_t)-1)
		goto error;

	pmp->pm_kiconvcookie = fcp;
	pmp->pm_flags |= MSDOSFS_CONVERTFNAME;
	return 0;

error:
	if (fcp->duh != (kiconv_t)-1)
		kiconv_close(fcp->duh, &errno);
	if (fcp->udh != (kiconv_t)-1)
		kiconv_close(fcp->udh, &errno);
	if (fcp->idh != (kiconv_t)-1)
		kiconv_close(fcp->idh, &errno);
	if (fcp->dih != (kiconv_t)-1)
		kiconv_close(fcp->dih, &errno);
	if (fcp->dch != (kiconv_t)-1)
		kiconv_close(fcp->dch, &errno);
	if (fcp->cdh != (kiconv_t)-1)
		kiconv_close(fcp->cdh, &errno);
	if (fcp->cih != (kiconv_t)-1)
		kiconv_close(fcp->cih, &errno);
	kiconv_freelocale(fcp->l, &errno);
	free(fcp, M_MSDOSFSTMP);
	pmp->pm_kiconvcookie = NULL;
	pmp->pm_flags &= ~MSDOSFS_CONVERTFNAME;
	return -1;
}

void
msdosfs_finikiconv(struct msdosfsmount *pmp)
{
	struct msdosfs_kiconv *fcp = pmp->pm_kiconvcookie;
	int errno;

	if (fcp) {
		kiconv_close(fcp->duh, &errno);
		kiconv_close(fcp->udh, &errno);
		kiconv_close(fcp->idh, &errno);
		kiconv_close(fcp->dih, &errno);
		kiconv_close(fcp->dch, &errno);
		kiconv_close(fcp->cdh, &errno);
		kiconv_close(fcp->cih, &errno);
		kiconv_freelocale(fcp->l, &errno);
		free(fcp, M_MSDOSFSTMP);
	}
}
#else	/* !_KERNEL || _RUMPKERNEL || _RUMP_NATIVE_ABI */
int
msdosfs_initkiconv(struct msdosfsmount *pmp)
{

	pmp->pm_kiconvcookie = NULL;
	pmp->pm_flags &= ~MSDOSFS_CONVERTFNAME;
	return 0;
}

/*ARGSUSED*/
void
msdosfs_finikiconv(struct msdosfsmount *pmp)
{

	/* Nothing to do */
}
#endif

/*
 * Convert a DOS filename to a unix filename. And, return the number of
 * characters in the resulting unix filename excluding the terminating
 * null.
 */
#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
static int
dos2unixfn_kiconv(u_char dn[11], u_char *un, int lower,
    struct msdosfsmount *pmp)
{
	struct msdosfs_kiconv *fcp = pmp->pm_kiconvcookie;
	u_char *p, *tmptop;
	const char *src;
	char *dst;
	wchar_t *wtop;
	const wchar_t *wbuf;
	size_t szsrc, szdst, szresult;
	size_t sz, wsz;
	mbstate_t mbs;
	int has_ext = 0;
	int errno;
	size_t i;
	int j;

	/*
	 * If first char of the filename is SLOT_E5 (0x05),
	 * then the real first char of the filename should
	 * be 0xe5. But, they couldn't just have a 0xe5
	 * mean 0xe5 because that is used to mean a freed
	 * directory slot. Another dos quirk.
	 */
	p = un;
	*p++ = (*dn == SLOT_E5) ? SLOT_DELETED : *dn;

	/*
	 * Copy the rest into the unix filename string,
	 * ignoring trailing blanks.
	 */
	for (j = 7; (j >= 0) && (dn[j] == ' '); j--)
		continue;

	for (i = 1; i <= j; i++)
		*p++ = dn[i];
	dn += 8;

	/*
	 * Now, if there is an extension then put in a period
	 * and copy in the extension.
	 */
	if (*dn != ' ') {
		has_ext = 1;
		*p++ = '.';
		for (i = 0; i < 3 && *dn != ' '; i++)
			*p++ = *dn++;
	}

	if (!lower || (!(lower & LCASE_BASE) && !has_ext)) {
		/* fast path */

		/* initialize handle */
		(void)kiconv_conv(fcp->cih, NULL, NULL, NULL, NULL, &errno);

		/*
		 * convert from codepage to iocharset
		 */
		src = un;
		szsrc = p - un;
		dst = tmptop = PNBUF_GET();
		KASSERT(tmptop != NULL);
		szdst = MAXNAMLEN + 1;
		szresult = kiconv_conv(fcp->cih, &src, &szsrc, &dst, &szdst,
		    &errno);
		if (szresult == (size_t)-1) {
			PNBUF_PUT(tmptop);
			return -1;
		}
		sz = MAXNAMLEN + 1 - szdst;

		memcpy(un, tmptop, sz);
		un[sz] = '\0';
		PNBUF_PUT(tmptop);

		return sz;
	}
	/* slow path */

	/* initialize handle */
	(void)kiconv_conv(fcp->cdh, NULL, NULL, NULL, NULL, &errno);
	(void)kiconv_conv(fcp->dih, NULL, NULL, NULL, NULL, &errno);

	/*
	 * convert from codepage to default encoding.
	 */
	src = un;
	szsrc = p - un;
	dst = tmptop = PNBUF_GET();
	KASSERT(tmptop != NULL);
	szdst = PATH_MAX;
	szresult = kiconv_conv(fcp->cdh, &src, &szsrc, &dst, &szdst, &errno);
	if (szresult == (size_t)-1)
		goto free_tmp;
	sz = PATH_MAX - szdst;
	tmptop[sz] = '\0';

	/*
	 * convert default encoding to wchar_t
	 */
	src = tmptop;
	wtop = WPNBUF_GET();
	KASSERT(wtop != NULL);
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_mbsrtowcs_l(wtop, &src, PATH_MAX, &mbs, fcp->l,
	    &errno);
	if (szresult == (size_t)-1)
		goto free_wbuf;
	wsz = szresult;
	wtop[wsz] = L'\0';

	/*
	 * tolower
	 */
	if (!has_ext) {
		j = -1;
		for (i = 0; i < wsz; i++) {
			if (wtop[i] == L'.')
				j = i + 1;
		}
		if (j < 0)
			goto no_ext;

		if (lower & LCASE_BASE) {
			for (i = 0; i < j - 1; i++) {
				if (kiconv_iswupper_l(wtop[i], fcp->l)) {
					wtop[i] = kiconv_towlower_l(wtop[i],
					    fcp->l);
				}
			}
		}
		if (lower & LCASE_EXT) {
			for (i = j; i < wsz; i++) {
				if (kiconv_iswupper_l(wtop[i], fcp->l)) {
					wtop[i] = kiconv_towlower_l(wtop[i],
					    fcp->l);
				}
			}
		}
	} else {
no_ext:
		if (lower & LCASE_BASE) {
			for (i = 0; i < wsz; i++) {
				if (kiconv_iswupper_l(wtop[i], fcp->l)) {
					wtop[i] = kiconv_towlower_l(wtop[i],
					    fcp->l);
				}
			}
		}
	}

	/*
	 * convert from wchar_t to default encoding
	 */
	wbuf = wtop;
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_wcsrtombs_l(tmptop, &wbuf, PATH_MAX, &mbs, fcp->l,
	    &errno);
	if (szresult == (size_t)-1)
		goto free_wbuf;
	WPNBUF_PUT(wtop);

	/*
	 * convert from default encoding to iocharset
	 */
	src = tmptop;
	szsrc = szresult;
	dst = un;
	szdst = MAXNAMLEN + 1;
	szresult = kiconv_conv(fcp->dih, &src, &szsrc, &dst, &szdst, &errno);
	if (szresult == (size_t)-1)
		goto free_tmp;
	PNBUF_PUT(tmptop);
	sz = MAXNAMLEN + 1 - szdst;
	un[sz] = '\0';

	return sz;

free_wbuf:
	WPNBUF_PUT(wtop);
free_tmp:
	PNBUF_PUT(tmptop);
	return -1;
}

static __inline bool
is_skip_char(wchar_t wc)
{

	return (wc == L' ') || (wc == L'.');
}

static __inline bool
is_bad_char(wchar_t wc)
{

	/* XXX: 0x00-0x1f isn't PCS. use iswprint()? */
	return (wc < 0x20)
	    || (wc == L'"') || (wc == L'*') || (wc == L'/') || (wc == L':')
	    || (wc == L'<') || (wc == L'=') || (wc == L'>') || (wc == L'?')
	    || (wc == L'|') || (wc == L'\\');
}

static __inline wchar_t
is_replace_char(wchar_t wc)
{

	return (wc == L'+') || (wc == L',') || (wc == L';')
	    || (wc == L'[') || (wc == L']');
}

static size_t
to_codepage_char(u_char *top, size_t len, wchar_t wc, int *conv, mbstate_t *mbs,
    struct msdosfs_kiconv *fcp, int *errnop)
{
	u_char buf[MB_LEN_MAX];
	wchar_t wtop[2];
	const wchar_t *wbuf;
	const char *src;
	char *dst;
	size_t szsrc, szdst, szresult;

	if (is_skip_char(wc)) {
		*conv = 3;
		return 0;
	}

	if (is_replace_char(wc))
		goto replace;

	if (kiconv_iswlower_l(wc, fcp->l)) {
		wc = kiconv_towupper_l(wc, fcp->l);
		if (*conv != 3)
			*conv = 2;
	}

	wtop[0] = wc;
	wtop[1] = L'\0';
	wbuf = wtop;
	szresult = kiconv_wcsrtombs_l(buf, &wbuf, sizeof(buf), mbs, fcp->l,
	    errnop);
	if (szresult == (size_t)-1) {
		memset(mbs, 0, sizeof(*mbs));
		goto replace;
	}

	src = buf;
	szsrc = szresult;
	dst = top;
	szdst = len;
	szresult = kiconv_conv(fcp->dch, &src, &szsrc, &dst, &szdst, errnop);
	if (szresult == (size_t)-1) {
		int errno2;
		(void)kiconv_conv(fcp->dch, NULL, NULL, NULL, NULL, &errno2);
		goto replace;
	}
	return len - szdst;

replace:
	*conv = 3;
	top[0] = '_';
	return 1;
}

/*
 * Returns
 *	0 if name couldn't be converted
 *	1 if the converted name is the same as the original
 *	  (no long filename entry necessary for Win95)
 *	2 if conversion was successful
 *	3 if conversion was successful and generation number was inserted
 */
static int
unix2dosfn_kiconv(struct msdosfs_winfn *fn, u_char dn[12], u_int gen,
    struct msdosfsmount *pmp)
{
	struct msdosfs_kiconv *fcp = pmp->pm_kiconvcookie;
	wchar_t *wcs = fn->wcs;
	size_t wcslen = fn->wcslen;
	u_char *base = &dn[0], *ext = &dn[8];
	u_char gentext[6], *wcp;
	u_char buf[12];
	int coff[8], ncoff;
	size_t szresult;
	mbstate_t mbs;
	wchar_t wc;
	int errno;
	const wchar_t *cp, *ep, *dp, *dp1;
	int i, j, l;
	int baselen, extlen;
	int conv = 1;

	/*
	 * The filenames "." and ".." are handled specially, since they
	 * don't follow dos filename rules.
	 */
	if (wcslen == 1 && wcs[0] == L'.') {
		dn[0] = '.';
		return gen <= 1;
	}
	if (wcslen == 2 && wcs[0] == L'.' && wcs[1] == L'.') {
		dn[0] = '.';
		dn[1] = '.';
		return gen <= 1;
	}

	/*
	 * Filenames with only blanks and dots are not allowed!
	 * Also filenames with bad character are not allowed.
	 */
	for (cp = wcs, i = wcslen; --i >= 0; cp++) {
		if (!is_skip_char(*cp)) {
			if (is_bad_char(*cp))
				return 0;
			break;
		}
	}
	if (i < 0)
		return 0;

	/*
	 * Now find the extension.
	 * Note: dot as first char doesn't start extension
	 *	 and trailing dots and blanks are ignored
	 */
	dp = dp1 = NULL;
	ep = wcs + wcslen;
	for (cp = wcs + 1, i = wcslen - 1; --i >= 0;) {
		wc = *cp++;
		switch (wc) {
		case L'.':
			if (dp1 == NULL)
				dp1 = cp;
			break;
		case L' ':
			break;
		default:
			if (dp1) {
				ep = dp1 - 1;
				dp = dp1;
			}
			dp1 = NULL;
			break;
		}
	}
	if (dp) {
		/* In "...test" case, extention as basename and no extention. */
		for (cp = wcs; cp < dp; cp++) {
			if (!is_skip_char(*cp))
				break;
		}
		if (cp == dp) {
			dp = NULL;
			ep = wcs + wcslen;
		}
	}

	/* initialize handle */
	(void)kiconv_conv(fcp->dch, NULL, NULL, NULL, NULL, &errno);

	/*
	 * Now convert it
	 */
	/* base filename */
	ncoff = 0;
	baselen = 0;
	memset(&mbs, 0, sizeof(mbs));
	for (cp = wcs; cp < ep; cp++) {
		szresult = to_codepage_char(buf, sizeof(buf), *cp, &conv, &mbs,
		    fcp, &errno);
		if (szresult == 0)
			continue;

		if (baselen >= 2)
			coff[ncoff++] = baselen;
		if (baselen + szresult > 8) {
			conv = 3;
			break;
		}
		memcpy(base + baselen, buf, szresult);
		baselen += szresult;
	}
	if (baselen == 0)
		return 0;

	/* ext */
	extlen = 0;
	memset(&mbs, 0, sizeof(mbs));
	if (dp) {
		if (dp1)
			l = dp1 - dp;
		else
			l = wcslen - (dp - wcs);
		ep = dp + l;
		for (cp = dp; cp < ep; cp++) {
			szresult = to_codepage_char(buf, sizeof(buf), *cp,
			    &conv, &mbs, fcp, &errno);
			if (szresult == 0)
				continue;

			if (extlen + szresult > 3) {
				conv = 3;
				break;
			}
			memcpy(ext + extlen, buf, szresult);
			extlen += szresult;
		}
	}

	/*
	 * The first character cannot be E5,
	 * because that means a deleted entry
	 */
	if (base[0] == SLOT_DELETED)
		base[0] = SLOT_E5;

	/*
	 * If there wasn't any char dropped,
	 * there is no place for generation numbers
	 */
	if (conv != 3) {
		if (gen > 1)
			return 0;
		return conv;
	}

	/*
	 * Now insert the generation number into the filename part
	 */
	for (wcp = gentext + sizeof(gentext); wcp > gentext && gen; gen /= 10)
		*--wcp = gen % 10 + '0';
	if (gen)
		return 0;
	for (i = 8; dn[--i] == ' ';)
		continue;
	i++;
	l = gentext + sizeof(gentext) - wcp + 1;
	if (l > 8 - i) {
		i = 8 - l;
		if (ncoff > 0) {
			for (j = ncoff; --j >= 0;) {
				if (l <= 8 - coff[j]) {
					i = coff[j];
					break;
				}
			}
			if (j < 0)
				return 0;
		}
	}
	dn[i++] = '~';
	while (wcp < gentext + sizeof(gentext))
		dn[i++] = *wcp++;
	return 3;
}

static int
unix2winfn_kiconv(struct msdosfs_winfn *fn, struct winentry *wep, int cnt,
    int chksum, struct msdosfsmount *pmp)
{
	const u_char *wn = fn->wn;
	int wnlen = fn->wnlen;
	int sz;
	uint8_t *wcp;
	int i;

	sz = (cnt - 1) * WIN_CHARS;
	wn += sz * 2;
	wnlen -= sz;

	/*
	 * Initialize winentry to some useful default
	 */
	memset(wep, 0xff, sizeof(*wep));
	wep->weCnt = cnt;
	wep->weAttributes = ATTR_WIN95;
	wep->weReserved1 = 0;
	wep->weChksum = chksum;
	wep->weReserved2 = 0;

	/*
	 * Now convert the filename parts
	 */
	for (wcp = wep->wePart1, i = 0; i < sizeof(wep->wePart1) / 2; i++) {
		if (--wnlen < 0)
			goto done;
		*wcp++ = *wn++;
		*wcp++ = *wn++;
	}
	for (wcp = wep->wePart2, i = 0; i < sizeof(wep->wePart2) / 2; i++) {
		if (--wnlen < 0)
			goto done;
		*wcp++ = *wn++;
		*wcp++ = *wn++;
	}
	for (wcp = wep->wePart3, i = 0; i < sizeof(wep->wePart3) / 2; i++) {
		if (--wnlen < 0)
			goto done;
		*wcp++ = *wn++;
		*wcp++ = *wn++;
	}
	if (wnlen == 0)
		wep->weCnt |= WIN_LAST;
	return wnlen;

done:
	*wcp++ = '\0';
	*wcp++ = '\0';
	wep->weCnt |= WIN_LAST;
	return 0;
}

static int
win2unixfn_kiconv(struct winentry *wep, struct dirent *dp, int chksum,
    struct msdosfsmount *pmp)
{
	struct msdosfs_kiconv *fcp = pmp->pm_kiconvcookie;
	uint8_t buf[sizeof(wep->wePart1) + sizeof(wep->wePart2) + sizeof(wep->wePart3)];
	char *p, *tmptop, *untop;
	const char *src;
	char *dst;
	wchar_t *wtop;
	const wchar_t *wbuf;
	size_t szsrc, szdst, szresult;
	size_t sz, wsz;
	mbstate_t mbs;
	int errno;
	size_t i;

	/*
	 * First compare checksums
	 */
	if (wep->weCnt & WIN_LAST) {
		chksum = wep->weChksum;
		dp->d_namlen = 0;
	} else if (chksum != wep->weChksum)
		chksum = -1;
	if (chksum == -1)
		goto error;

	p = buf; sz = sizeof(wep->wePart1); memcpy(p, wep->wePart1, sz);
	p += sz; sz = sizeof(wep->wePart2); memcpy(p, wep->wePart2, sz);
	p += sz; sz = sizeof(wep->wePart3); memcpy(p, wep->wePart3, sz);

	/* initialize handle */
	(void)kiconv_conv(fcp->udh, NULL, NULL, NULL, NULL, &errno);
	(void)kiconv_conv(fcp->dih, NULL, NULL, NULL, NULL, &errno);

	/*
	 * convert from UTF-16LE to default encoding.
	 */
	src = buf;
	szsrc = sizeof(buf);
	dst = tmptop = PNBUF_GET();
	KASSERT(tmptop != NULL);
	szdst = PATH_MAX;
	szresult = kiconv_conv(fcp->udh, &src, &szsrc, &dst, &szdst, &errno);
	if (szresult == (size_t)-1)
		goto free_tmp;
	sz = PATH_MAX - szdst;
	tmptop[sz] = '\0';

	/*
	 * convert default encoding to wchar_t
	 */
	src = tmptop;
	wtop = WPNBUF_GET();
	KASSERT(wtop != NULL);
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_mbsrtowcs_l(wtop, &src, PATH_MAX, &mbs, fcp->l,
	    &errno);
	if (szresult == (size_t)-1)
		goto free_wbuf;
	wsz = szresult;
	wtop[wsz] = L'\0';

	/*
	 * Convert the name parts
	 */
	for (i = 0; i < wsz; i++) {
		if (wtop[i] == L'/') {
			goto free_wbuf;
		}
	}

	/*
	 * convert from wchar_t to default encoding
	 */
	wbuf = wtop;
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_wcsrtombs_l(tmptop, &wbuf, sizeof(dp->d_name), &mbs,
	    fcp->l, &errno);
	if (szresult == (size_t)-1)
		goto free_wbuf;
	WPNBUF_PUT(wtop);

	/*
	 * convert from default encoding to iocharset
	 */
	src = tmptop;
	szsrc = szresult;
	dst = untop = PNBUF_GET();
	KASSERT(untop != NULL);
	szdst = sizeof(dp->d_name) - dp->d_namlen;
	szresult = kiconv_conv(fcp->dih, &src, &szsrc, &dst, &szdst, &errno);
	if (szresult == (size_t)-1) {
		PNBUF_PUT(untop);
		goto free_tmp;
	}
	PNBUF_PUT(tmptop);
	sz = sizeof(dp->d_name) - dp->d_namlen - szdst;
	memmove(dp->d_name + sz, dp->d_name, dp->d_namlen);
	memcpy(dp->d_name, untop, sz);
	PNBUF_PUT(untop);
	dp->d_namlen += sz;
	dp->d_name[dp->d_namlen] = '\0';

	return chksum;

free_wbuf:
	WPNBUF_PUT(wtop);
free_tmp:
	PNBUF_PUT(tmptop);
error:
	return -1;
}

static int
winChkName_kiconv(struct msdosfs_winfn *fn, struct winentry *wep, int chksum,
    struct msdosfsmount *pmp)
{
	struct msdosfs_kiconv *fcp = pmp->pm_kiconvcookie;
	const u_char *wn = fn->lcwn;
	int wnlen = fn->wnlen;
	uint8_t buf[sizeof(wep->wePart1) + sizeof(wep->wePart2) + sizeof(wep->wePart3)];
	u_char *p, *tmptop;
	const char *src;
	char *dst;
	wchar_t *wtop;
	const wchar_t *wbuf;
	size_t szsrc, szdst, szresult;
	size_t sz, wsz;
	mbstate_t mbs;
	int errno;
	size_t i;

	/*
	 * Offset of this entry
	 */
	i = ((wep->weCnt & WIN_CNT) - 1) * WIN_CHARS;
	wn += i * 2;
	wnlen -= i;
	if (wnlen < 0)
		goto error;

	/*
	 * Ignore redundant winentries (those with only \0\0 on start in them).
	 * An appearance of such entry is a bug; unknown if in NetBSD msdosfs
	 * or MS Windows.
	 */
	if (wnlen == 0) {
		if (wep->wePart1[0] == '\0' && wep->wePart1[1] == '\0')
			goto done;
		goto error;
	}

	if ((wep->weCnt & WIN_LAST) && wnlen > WIN_CHARS)
		goto error;

	p = buf; sz = sizeof(wep->wePart1); memcpy(p, wep->wePart1, sz);
	p += sz; sz = sizeof(wep->wePart2); memcpy(p, wep->wePart2, sz);
	p += sz; sz = sizeof(wep->wePart3); memcpy(p, wep->wePart3, sz);

	/* initialize handle */
	(void)kiconv_conv(fcp->udh, NULL, NULL, NULL, NULL, &errno);
	(void)kiconv_conv(fcp->duh, NULL, NULL, NULL, NULL, &errno);

	/*
	 * convert from UTF-16LE to default encoding.
	 */
	src = buf;
	szsrc = sizeof(buf);
	dst = tmptop = PNBUF_GET();
	KASSERT(tmptop != NULL);
	szdst = PATH_MAX;
	szresult = kiconv_conv(fcp->udh, &src, &szsrc, &dst, &szdst, &errno);
	if (szresult == (size_t)-1)
		goto free_tmp;
	sz = PATH_MAX - szdst;
	tmptop[sz] = '\0';

	/*
	 * convert default encoding to wchar_t
	 */
	src = tmptop;
	wtop = WPNBUF_GET();
	KASSERT(wtop != NULL);
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_mbsrtowcs_l(wtop, &src, PATH_MAX, &mbs, fcp->l,
	    &errno);
	if (szresult == (size_t)-1)
		goto free_wbuf;
	wsz = szresult;
	wtop[wsz] = L'\0';

	/*
	 * tolower
	 */
	for (i = 0; i < wsz; i++) {
		if (kiconv_iswupper_l(wtop[i], fcp->l)) {
			wtop[i] = kiconv_towlower_l(wtop[i], fcp->l);
		}
	}

	/*
	 * convert from wchar_t to default encoding (lower-case)
	 */
	wbuf = wtop;
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_wcsrtombs_l(tmptop, &wbuf, PATH_MAX, &mbs, fcp->l,
	    &errno);
	WPNBUF_PUT(wtop);
	if (szresult == (size_t)-1)
		goto free_tmp;

	/*
	 * convert from default encoding to UTF-16LE (lower-case)
	 */
	src = tmptop;
	szsrc = szresult;
	dst = buf;
	szdst = sizeof(buf);
	szresult = kiconv_conv(fcp->duh, &src, &szsrc, &dst, &szdst, &errno);
	PNBUF_PUT(tmptop);
	if (szresult == (size_t)-1)
		goto error;
	KASSERT((sizeof(buf) - szdst) == sizeof(buf));

	/*
	 * Compare the name parts
	 */
	sz = min(wnlen, sizeof(buf) / 2);
	if (memcmp(buf, wn, sz * 2))
		goto error;
	wnlen -= sz;
	wn += sz * 2;
	if (wnlen == 0 && sz != (sizeof(buf) / 2))
		if (wn[0] != '\0' || wn[1] != '\0')
			goto error;

done:
	return chksum;

free_wbuf:
	WPNBUF_PUT(wtop);
free_tmp:
	PNBUF_PUT(tmptop);
error:
	return -1;
}

static int
newwinfn_kiconv(struct componentname *cnp, struct msdosfs_winfn **fnp,
    struct msdosfsmount *pmp)
{
	struct msdosfs_kiconv *fcp = pmp->pm_kiconvcookie;
	struct msdosfs_winfn *fn;
	char *tmptop, *wintop, *winlctop;
	const char *src;
	char *dst;
	wchar_t *wtop, *wtmp;
	const wchar_t *wbuf;
	size_t szsrc, szdst, szresult;
	size_t sz, lcsz, wsz, orig_wsz;
	mbstate_t mbs;
	int errno;
	int error;
	size_t i;

	/* initialize handle */
	(void)kiconv_conv(fcp->idh, NULL, NULL, NULL, NULL, &errno);
	(void)kiconv_conv(fcp->duh, NULL, NULL, NULL, NULL, &errno);

	/*
	 * convert from iocharset to default encoding.
	 */
	src = (const u_char *)cnp->cn_nameptr;
	szsrc = cnp->cn_namelen;
	dst = tmptop = PNBUF_GET();
	KASSERT(tmptop != NULL);
	szdst = PATH_MAX;
	szresult = kiconv_conv(fcp->idh, &src, &szsrc, &dst, &szdst, &errno);
	if (szresult == (size_t)-1) {
		error = errno;
		goto free_tmp;
	}
	sz = PATH_MAX - szdst;
	tmptop[sz] = '\0';

	/*
	 * convert default encoding to wchar_t
	 */
	src = tmptop;
	wtop = WPNBUF_GET();
	KASSERT(wtop != NULL);
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_mbsrtowcs_l(wtop, &src, PATH_MAX, &mbs, fcp->l,
	    &errno);
	if (szresult == (size_t)-1) {
		error = errno;
		goto free_wbuf;
	}
	wsz = szresult;
	wtop[wsz] =L'\0';

	/*
	 * Drop trailing blanks and dots for namelen
	 */
	wtmp = WPNBUF_GET();
	KASSERT(wtmp != NULL);
	memcpy(wtmp, wtop, PATH_MAX * sizeof(wchar_t));
	orig_wsz = wsz;
	for (wbuf = wtmp + wsz; wsz > 0; wsz--) {
		if (*--wbuf != L' ' && *wbuf != L'.') {
			break;
		}
	}
	wtmp[wsz] = L'\0';

	/*
	 * convert from wchar_t to default encoding for namelen.
	 */
	wbuf = wtmp;
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_wcsrtombs_l(tmptop, &wbuf, PATH_MAX, &mbs, fcp->l,
	    &errno);
	if (szresult == (size_t)-1) {
		error = errno;
		goto free_wtmp;
	}

	/*
	 * convert from default encoding to UTF-16LE for namelen
	 */
	src = tmptop;
	szsrc = szresult;
	dst = wintop = PNBUF_GET();
	KASSERT(wintop != NULL);
	szdst = PATH_MAX;
	szresult = kiconv_conv(fcp->duh, &src, &szsrc, &dst, &szdst, &errno);
	if (szresult == (size_t)-1) {
		error = errno;
		goto free_wintop;
	}
	sz = PATH_MAX - szdst;
	KASSERT((sz % 2) == 0);
	sz /= 2;

	if (sz > WIN_MAXLEN) {
		error = ENAMETOOLONG;
		goto free_wintop;
	}
	wsz = orig_wsz;

	/*
	 * convert from wchar_t to default encoding
	 */
	wbuf = wtop;
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_wcsrtombs_l(tmptop, &wbuf, PATH_MAX, &mbs, fcp->l,
	    &errno);
	if (szresult == (size_t)-1) {
		error = errno;
		goto free_wintop;
	}

	/*
	 * convert from default encoding to UTF-16LE
	 */
	/* initialize handle */
	(void)kiconv_conv(fcp->duh, NULL, NULL, NULL, NULL, &errno);
	src = tmptop;
	szsrc = szresult;
	dst = wintop;
	szdst = PATH_MAX;
	szresult = kiconv_conv(fcp->duh, &src, &szsrc, &dst, &szdst, &errno);
	if (szresult == (size_t)-1) {
		error = errno;
		goto free_wintop;
	}
	sz = PATH_MAX - szdst;
	KASSERT((sz % 2) == 0);
	wintop[sz] = wintop[sz + 1] = '\0';
	sz /= 2;

	/*
	 * tolower
	 */
	memcpy(wtmp, wtop, PATH_MAX * sizeof(wchar_t));
	for (i = 0; i < wsz; i++) {
		if (kiconv_iswupper_l(wtmp[i], fcp->l)) {
			wtmp[i] = kiconv_towlower_l(wtmp[i], fcp->l);
		}
	}

	/*
	 * convert from wchar_t to default encoding (lower-case)
	 */
	wbuf = wtmp;
	memset(&mbs, 0, sizeof(mbs));
	szresult = kiconv_wcsrtombs_l(tmptop, &wbuf, PATH_MAX, &mbs, fcp->l,
	    &errno);
	if (szresult == (size_t)-1) {
		error = errno;
		goto free_wintop;
	}

	/*
	 * convert from default encoding to UTF-16LE (lower-case)
	 */
	/* initialize handle */
	(void)kiconv_conv(fcp->duh, NULL, NULL, NULL, NULL, &errno);
	src = tmptop;
	szsrc = szresult;
	dst = winlctop = PNBUF_GET();
	KASSERT(winlctop != NULL);
	szdst = PATH_MAX;
	szresult = kiconv_conv(fcp->duh, &src, &szsrc, &dst, &szdst, &errno);
	if (szresult == (size_t)-1) {
		error = errno;
		goto free_winlctop;
	}
	lcsz = PATH_MAX - szdst;
	KASSERT((lcsz % 2) == 0);
	KASSERT((lcsz / 2) == sz);
	winlctop[lcsz] = winlctop[lcsz + 1] = '\0';

	WPNBUF_PUT(wtmp);
	PNBUF_PUT(tmptop);

	fn = malloc(sizeof(*fn), M_MSDOSFSTMP, M_WAITOK);
	fn->un = (const u_char *)cnp->cn_nameptr;
	fn->unlen = cnp->cn_namelen;
	fn->wn = wintop;
	fn->lcwn = winlctop;
	fn->wnlen = sz;
	fn->wcs = wtop;
	fn->wcslen = wsz;

	*fnp = fn;
	return 0;

free_winlctop:
	PNBUF_PUT(winlctop);
free_wintop:
	PNBUF_PUT(wintop);
free_wtmp:
	WPNBUF_PUT(wtmp);
free_wbuf:
	WPNBUF_PUT(wtop);
free_tmp:
	PNBUF_PUT(tmptop);
	return error;
}
#endif

int
dos2unixfn(u_char dn[11], u_char *un, int lower, struct msdosfsmount *pmp)
{
	int i, j;
	int thislong;
	u_char c;

#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
	if (pmp->pm_flags & MSDOSFS_CONVERTFNAME) {
		thislong = dos2unixfn_kiconv(dn, un, lower, pmp);
		if (thislong >= 0)
			return thislong;
		/*FALLTHROUGH*/
		printf("%s: warning: filename convertion failure.\n", __func__);
	}
#endif

	thislong = 1;

	/*
	 * If first char of the filename is SLOT_E5 (0x05), then the real
	 * first char of the filename should be 0xe5. But, they couldn't
	 * just have a 0xe5 mean 0xe5 because that is used to mean a freed
	 * directory slot. Another dos quirk.
	 */
	if (*dn == SLOT_E5)
		c = dos2unix[SLOT_DELETED];
	else
		c = dos2unix[*dn];
	*un++ = (lower & LCASE_BASE) ? u2l[c] : c;

	/*
	 * Copy the rest into the unix filename string, ignoring
	 * trailing blanks.
	 */

	for (j=7; (j >= 0) && (dn[j] == ' '); j--)
		;

	for (i = 1; i <= j; i++) {
		c = dos2unix[dn[i]];
		*un++ = (lower & LCASE_BASE) ? u2l[c] : c;
		thislong++;
	}
	dn += 8;

	/*
	 * Now, if there is an extension then put in a period and copy in
	 * the extension.
	 */
	if (*dn != ' ') {
		*un++ = '.';
		thislong++;
		for (i = 0; i < 3 && *dn != ' '; i++) {
			c = dos2unix[*dn++];
			*un++ = (lower & LCASE_EXT) ? u2l[c] : c;
			thislong++;
		}
	}
	*un++ = 0;

	return (thislong);
}

/*
 * Convert a unix filename to a DOS filename according to Win95 rules.
 * If applicable and gen is not 0, it is inserted into the converted
 * filename as a generation number.
 * Returns
 *	0 if name couldn't be converted
 *	1 if the converted name is the same as the original
 *	  (no long filename entry necessary for Win95)
 *	2 if conversion was successful
 *	3 if conversion was successful and generation number was inserted
 */
int
unix2dosfn(struct msdosfs_winfn *fn, u_char dn[12], u_int gen,
    struct msdosfsmount *pmp)
{
	const u_char *un = fn->un;
	int unlen = fn->unlen;
	int i, j, l;
	int conv = 1;
	const u_char *cp, *dp, *dp1;
	u_char gentext[6], *wcp;
	int shortlen;

	/*
	 * Fill the dos filename string with blanks. These are DOS's pad
	 * characters.
	 */
	for (i = 0; i < 11; i++)
		dn[i] = ' ';
	dn[11] = 0;

#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
	if (pmp->pm_flags & MSDOSFS_CONVERTFNAME)
		return unix2dosfn_kiconv(fn, dn, gen, pmp);
#endif

	/*
	 * The filenames "." and ".." are handled specially, since they
	 * don't follow dos filename rules.
	 */
	if (un[0] == '.' && unlen == 1) {
		dn[0] = '.';
		return gen <= 1;
	}
	if (un[0] == '.' && un[1] == '.' && unlen == 2) {
		dn[0] = '.';
		dn[1] = '.';
		return gen <= 1;
	}

	/*
	 * Filenames with only blanks and dots are not allowed!
	 */
	for (cp = un, i = unlen; --i >= 0; cp++)
		if (*cp != ' ' && *cp != '.')
			break;
	if (i < 0)
		return 0;

	/*
	 * Now find the extension
	 * Note: dot as first char doesn't start extension
	 *	 and trailing dots and blanks are ignored
	 */
	dp = dp1 = 0;
	for (cp = un + 1, i = unlen - 1; --i >= 0;) {
		switch (*cp++) {
		case '.':
			if (!dp1)
				dp1 = cp;
			break;
		case ' ':
			break;
		default:
			if (dp1)
				dp = dp1;
			dp1 = 0;
			break;
		}
	}

	/*
	 * Now convert it
	 */
	if (dp) {
		if (dp1)
			l = dp1 - dp;
		else
			l = unlen - (dp - un);
		for (i = 0, j = 8; i < l && j < 11; i++, j++) {
			if (dp[i] != (dn[j] = unix2dos[dp[i]])
			    && conv != 3)
				conv = 2;
			if (!dn[j]) {
				conv = 3;
				dn[j--] = ' ';
			}
		}
		if (i < l)
			conv = 3;
		dp--;
	} else {
		for (dp = cp; *--dp == ' ' || *dp == '.';);
		dp++;
	}

	shortlen = (dp - un) <= 8;

	/*
	 * Now convert the rest of the name
	 */
	for (i = j = 0; un < dp && j < 8; i++, j++, un++) {
		if ((*un == ' ') && shortlen)
			dn[j] = ' ';
		else
			dn[j] = unix2dos[*un];
		if ((*un != dn[j])
		    && conv != 3)
			conv = 2;
		if (!dn[j]) {
			conv = 3;
			dn[j--] = ' ';
		}
	}
	if (un < dp)
		conv = 3;
	/*
	 * If we didn't have any chars in filename,
	 * generate a default
	 */
	if (!j)
		dn[0] = '_';

	/*
	 * The first character cannot be E5,
	 * because that means a deleted entry
	 */
	if (dn[0] == SLOT_DELETED)
		dn[0] = SLOT_E5;

	/*
	 * If there wasn't any char dropped,
	 * there is no place for generation numbers
	 */
	if (conv != 3) {
		if (gen > 1)
			return 0;
		return conv;
	}

	/*
	 * Now insert the generation number into the filename part
	 */
	for (wcp = gentext + sizeof(gentext); wcp > gentext && gen; gen /= 10)
		*--wcp = gen % 10 + '0';
	if (gen)
		return 0;
	for (i = 8; dn[--i] == ' ';);
	i++;
	if (gentext + sizeof(gentext) - wcp + 1 > 8 - i)
		i = 8 - (gentext + sizeof(gentext) - wcp + 1);
	dn[i++] = '~';
	while (wcp < gentext + sizeof(gentext))
		dn[i++] = *wcp++;
	return 3;
}

/*
 * Create a Win95 long name directory entry
 * Note: assumes that the filename is valid,
 *	 i.e. doesn't consist solely of blanks and dots
 */
int
unix2winfn(struct msdosfs_winfn *fn, struct winentry *wep, int cnt, int chksum,
    struct msdosfsmount *pmp)
{
	const u_char *un;
	int unlen;
	const u_int8_t *cp;
	u_int8_t *wcp;
	int i;

#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
	if (pmp->pm_flags & MSDOSFS_CONVERTFNAME)
		return unix2winfn_kiconv(fn, wep, cnt, chksum, pmp);
#endif

	un = fn->un;
	unlen = fn->unlen;

	/*
	 * Drop trailing blanks and dots
	 */
	for (cp = un + unlen; *--cp == ' ' || *cp == '.'; unlen--);

	un += (cnt - 1) * WIN_CHARS;
	unlen -= (cnt - 1) * WIN_CHARS;

	/*
	 * Initialize winentry to some useful default
	 */
	for (wcp = (u_int8_t *)wep, i = sizeof(*wep); --i >= 0; *wcp++ = 0xff);
	wep->weCnt = cnt;
	wep->weAttributes = ATTR_WIN95;
	wep->weReserved1 = 0;
	wep->weChksum = chksum;
	wep->weReserved2 = 0;

	/*
	 * Now convert the filename parts
	 */
	for (wcp = wep->wePart1, i = sizeof(wep->wePart1)/2; --i >= 0;) {
		if (--unlen < 0)
			goto done;
		*wcp++ = *un++;
		*wcp++ = 0;
	}
	for (wcp = wep->wePart2, i = sizeof(wep->wePart2)/2; --i >= 0;) {
		if (--unlen < 0)
			goto done;
		*wcp++ = *un++;
		*wcp++ = 0;
	}
	for (wcp = wep->wePart3, i = sizeof(wep->wePart3)/2; --i >= 0;) {
		if (--unlen < 0)
			goto done;
		*wcp++ = *un++;
		*wcp++ = 0;
	}
	if (!unlen)
		wep->weCnt |= WIN_LAST;
	return unlen;

done:
	*wcp++ = 0;
	*wcp++ = 0;
	wep->weCnt |= WIN_LAST;
	return 0;
}

/*
 * Compare our filename to the one in the Win95 entry
 * Returns the checksum or -1 if no match
 */
int
winChkName(struct msdosfs_winfn *fn, struct winentry *wep, int chksum,
    struct msdosfsmount *pmp)
{
	const u_char *un;
	int unlen;
	u_int8_t *cp;
	int i;

	/*
	 * First compare checksums
	 */
	if (wep->weCnt&WIN_LAST)
		chksum = wep->weChksum;
	else if (chksum != wep->weChksum)
		chksum = -1;
	if (chksum == -1)
		return -1;

#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
	if (pmp->pm_flags & MSDOSFS_CONVERTFNAME)
		return winChkName_kiconv(fn, wep, chksum, pmp);
#endif

	/*
	 * Offset of this entry
	 */
	un = fn->un;
	unlen = fn->unlen;
	i = ((wep->weCnt&WIN_CNT) - 1) * WIN_CHARS;
	un += i;
	if ((unlen -= i) < 0)
		return -1;

	/*
	 * Ignore redundant winentries (those with only \0\0 on start in them).
	 * An appearance of such entry is a bug; unknown if in NetBSD msdosfs
	 * or MS Windows.
	 */
	if (unlen == 0) {
		if (wep->wePart1[0] == '\0' && wep->wePart1[1] == '\0')
			return chksum;
		else
			return -1;
	}

	if ((wep->weCnt&WIN_LAST) && unlen > WIN_CHARS)
		return -1;

	/*
	 * Compare the name parts
	 */
	for (cp = wep->wePart1, i = sizeof(wep->wePart1)/2; --i >= 0;) {
		if (--unlen < 0) {
			if (!*cp++ && !*cp)
				return chksum;
			return -1;
		}
		if (u2l[*cp++] != u2l[*un++] || *cp++)
			return -1;
	}
	for (cp = wep->wePart2, i = sizeof(wep->wePart2)/2; --i >= 0;) {
		if (--unlen < 0) {
			if (!*cp++ && !*cp)
				return chksum;
			return -1;
		}
		if (u2l[*cp++] != u2l[*un++] || *cp++)
			return -1;
	}
	for (cp = wep->wePart3, i = sizeof(wep->wePart3)/2; --i >= 0;) {
		if (--unlen < 0) {
			if (!*cp++ && !*cp)
				return chksum;
			return -1;
		}
		if (u2l[*cp++] != u2l[*un++] || *cp++)
			return -1;
	}
	return chksum;
}

/*
 * Convert Win95 filename to dirbuf.
 * Returns the checksum or -1 if impossible
 */
int
win2unixfn(struct winentry *wep, struct dirent *dp, int chksum,
    struct msdosfsmount *pmp)
{
	u_int8_t *cp;
	u_int8_t *np, *ep = (u_int8_t *)dp->d_name + WIN_MAXLEN;
	int i;

	if ((wep->weCnt&WIN_CNT) > howmany(WIN_MAXLEN, WIN_CHARS)
	    || !(wep->weCnt&WIN_CNT))
		return -1;

#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
	if (pmp->pm_flags & MSDOSFS_CONVERTFNAME)
		return win2unixfn_kiconv(wep, dp, chksum, pmp);
#endif

	/*
	 * First compare checksums
	 */
	if (wep->weCnt&WIN_LAST) {
		chksum = wep->weChksum;
		/*
		 * This works even though d_namlen is one byte!
		 */
#ifdef __NetBSD__
		dp->d_namlen = (wep->weCnt&WIN_CNT) * WIN_CHARS;
#endif
	} else if (chksum != wep->weChksum)
		chksum = -1;
	if (chksum == -1)
		return -1;

	/*
	 * Offset of this entry
	 */
	i = ((wep->weCnt&WIN_CNT) - 1) * WIN_CHARS;
	np = (u_int8_t *)dp->d_name + i;

	/*
	 * Convert the name parts
	 */
	for (cp = wep->wePart1, i = sizeof(wep->wePart1)/2; --i >= 0;) {
		switch (*np++ = *cp++) {
		case 0:
#ifdef __NetBSD__
			dp->d_namlen -= sizeof(wep->wePart2)/2
			    + sizeof(wep->wePart3)/2 + i + 1;
#endif
			return chksum;
		case '/':
			np[-1] = 0;
			return -1;
		}
		/*
		 * The size comparison should result in the compiler
		 * optimizing the whole if away
		 */
		if (WIN_MAXLEN % WIN_CHARS < sizeof(wep->wePart1) / 2
		    && np > ep) {
			np[-1] = 0;
			return -1;
		}
		if (*cp++)
			return -1;
	}
	for (cp = wep->wePart2, i = sizeof(wep->wePart2)/2; --i >= 0;) {
		switch (*np++ = *cp++) {
		case 0:
#ifdef __NetBSD__
			dp->d_namlen -= sizeof(wep->wePart3)/2 + i + 1;
#endif
			return chksum;
		case '/':
			np[-1] = 0;
			return -1;
		}
		/*
		 * The size comparisons should be optimized away
		 */
		if (WIN_MAXLEN % WIN_CHARS >= sizeof(wep->wePart1) / 2
		    && WIN_MAXLEN % WIN_CHARS < (sizeof(wep->wePart1) + sizeof(wep->wePart2)) / 2
		    && np > ep) {
			np[-1] = 0;
			return -1;
		}
		if (*cp++)
			return -1;
	}
	for (cp = wep->wePart3, i = sizeof(wep->wePart3)/2; --i >= 0;) {
		switch (*np++ = *cp++) {
		case 0:
#ifdef __NetBSD__
			dp->d_namlen -= i + 1;
#endif
			return chksum;
		case '/':
			np[-1] = 0;
			return -1;
		}
		/*
		 * See above
		 */
		if (WIN_MAXLEN % WIN_CHARS >= (sizeof(wep->wePart1) + sizeof(wep->wePart2)) / 2
		    && np > ep) {
			np[-1] = 0;
			return -1;
		}
		if (*cp++)
			return -1;
	}
	return chksum;
}

/*
 * Compute the checksum of a DOS filename for Win95 use
 */
u_int8_t
winChksum(u_int8_t *name)
{
	int i;
	u_int8_t s;

	for (s = 0, i = 11; --i >= 0; s += *name++)
		s = (s << 7)|(s >> 1);
	return s;
}

/*
 * Determine the number of slots necessary for Win95 names
 */
int
winSlotCnt(struct msdosfs_winfn *fn, struct msdosfsmount *pmp)
{

#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
	if (pmp->pm_flags & MSDOSFS_CONVERTFNAME)
		return howmany(fn->wnlen, WIN_CHARS);
#endif
	return howmany(fn->unlen, WIN_CHARS);
}

int
newwinfn(struct componentname *cnp, struct msdosfs_winfn **fnp,
    struct msdosfsmount *pmp)
{
	struct msdosfs_winfn *fn;
	const u_char *un;
	int unlen;

#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
	if (pmp->pm_flags & MSDOSFS_CONVERTFNAME)
		return newwinfn_kiconv(cnp, fnp, pmp);
#endif

	un = (const u_char *)cnp->cn_nameptr;
	unlen = cnp->cn_namelen;
	for (un += unlen; unlen > 0; unlen--)
		if (*--un != ' ' && *un != '.')
			break;
	if (unlen > WIN_MAXLEN)
		return ENAMETOOLONG;

	fn = malloc(sizeof(*fn), M_MSDOSFSTMP, M_WAITOK);
	fn->un = (const u_char *)cnp->cn_nameptr;
	fn->unlen = cnp->cn_namelen;
#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
	fn->wn = fn->lcwn = NULL;
	fn->wnlen = 0;
	fn->wcs = NULL;
	fn->wcslen = 0;
#endif

	*fnp = fn;
	return 0;
}

void
freewinfn(struct msdosfs_winfn *fn, struct msdosfsmount *pmp)
{

	if (fn) {
#if defined(_KERNEL) && !defined(_RUMPKERNEL) && !defined(_RUMP_NATIVE_ABI)
		if (fn->wn)
			PNBUF_PUT(fn->wn);
		if (fn->lcwn)
			PNBUF_PUT(fn->lcwn);
		if (fn->wcs)
			WPNBUF_PUT(fn->wcs);
#endif
		free(fn, M_MSDOSFSTMP);
	}
}
