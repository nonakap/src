/*	$NetBSD: boot2.c,v 1.58 2012/08/04 03:51:27 riastradh Exp $	*/

/*-
 * Copyright (c) 2008, 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
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

/*
 * Copyright (c) 2003
 *	David Laight.  All rights reserved
 * Copyright (c) 1996, 1997, 1999
 * 	Matthias Drochner.  All rights reserved.
 * Copyright (c) 1996, 1997
 * 	Perry E. Metzger.  All rights reserved.
 * Copyright (c) 1997
 *	Jason R. Thorpe.  All rights reserved
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
 *    must display the following acknowledgements:
 *	This product includes software developed for the NetBSD Project
 *	by Matthias Drochner.
 *	This product includes software developed for the NetBSD Project
 *	by Perry E. Metzger.
 * 4. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Based on stand/biosboot/main.c */

#include <sys/types.h>
#include <sys/reboot.h>
#include <sys/bootblock.h>

#include <lib/libsa/stand.h>
#include <lib/libsa/ufs.h>
#include <lib/libkern/libkern.h>

#include <libi386.h>
#include <bootmod.h>
#include <bootmenu.h>
#include <vbe.h>
#include "devopen.h"

#ifdef SUPPORT_PS2
#include <biosmca.h>
#endif

extern struct x86_boot_params boot_params;

extern	const char bootprog_name[], bootprog_rev[], bootprog_kernrev[];

int errno;

int boot_biosdev;
daddr_t boot_biossector;

static const char * const names[][2] = {
	{ "netbsd", "netbsd.gz" },
	{ "onetbsd", "onetbsd.gz" },
	{ "netbsd.old", "netbsd.old.gz" },
};

#define NUMNAMES (sizeof(names)/sizeof(names[0]))
#define DEFFILENAME names[0][0]

#define MAXDEVNAME 16

static char *default_devname;
static int default_unit, default_partition;
static const char *default_filename;

char *sprint_bootsel(const char *);
void bootit(const char *, int, int);
void print_banner(void);
void boot2(int, uint64_t);

void	command_help(char *);
void	command_ls(char *);
void	command_quit(char *);
void	command_boot(char *);
void	command_dev(char *);
void	command_consdev(char *);
void	command_modules(char *);
void	command_multiboot(char *);
#if 1	/* XXX: XXXNONAKA */
void	command_memoryread_4(char *);
void	command_memorywrite_4(char *);
void	command_dump_pcicfg(char *);
void	command_write_pcicfg(char *);
#endif	/* XXX: XXXNONAKA */

const struct bootblk_command commands[] = {
	{ "help",	command_help },
	{ "?",		command_help },
	{ "ls",		command_ls },
	{ "quit",	command_quit },
	{ "boot",	command_boot },
	{ "dev",	command_dev },
	{ "consdev",	command_consdev },
	{ "modules",	command_modules },
	{ "load",	module_add },
	{ "multiboot",	command_multiboot },
	{ "vesa",	command_vesa },
	{ "splash",	splash_add },
	{ "rndseed",	rnd_add },
	{ "userconf",	userconf_add },
#if 1	/* XXX: XXXNONAKA */
	{ "mrd",	command_memoryread_4 },
	{ "mwd",	command_memorywrite_4 },
	{ "pcicfgr",	command_dump_pcicfg },
	{ "pcicfgw",	command_write_pcicfg },
#endif	/* XXX: XXXNONAKA */
	{ NULL,		NULL },
};

int
parsebootfile(const char *fname, char **fsname, char **devname,
	      int *unit, int *partition, const char **file)
{
	const char *col;

	*fsname = "ufs";
	*devname = default_devname;
	*unit = default_unit;
	*partition = default_partition;
	*file = default_filename;

	if (fname == NULL)
		return 0;

	if ((col = strchr(fname, ':')) != NULL) {	/* device given */
		static char savedevname[MAXDEVNAME+1];
		int devlen;
		int u = 0, p = 0;
		int i = 0;

		devlen = col - fname;
		if (devlen > MAXDEVNAME)
			return EINVAL;

#define isvalidname(c) ((c) >= 'a' && (c) <= 'z')
		if (!isvalidname(fname[i]))
			return EINVAL;
		do {
			savedevname[i] = fname[i];
			i++;
		} while (isvalidname(fname[i]));
		savedevname[i] = '\0';

#define isnum(c) ((c) >= '0' && (c) <= '9')
		if (i < devlen) {
			if (!isnum(fname[i]))
				return EUNIT;
			do {
				u *= 10;
				u += fname[i++] - '0';
			} while (isnum(fname[i]));
		}

#define isvalidpart(c) ((c) >= 'a' && (c) <= 'z')
		if (i < devlen) {
			if (!isvalidpart(fname[i]))
				return EPART;
			p = fname[i++] - 'a';
		}

		if (i != devlen)
			return ENXIO;

		*devname = savedevname;
		*unit = u;
		*partition = p;
		fname = col + 1;
	}

	if (*fname)
		*file = fname;

	return 0;
}

char *
sprint_bootsel(const char *filename)
{
	char *fsname, *devname;
	int unit, partition;
	const char *file;
	static char buf[80];

	if (parsebootfile(filename, &fsname, &devname, &unit,
			  &partition, &file) == 0) {
		sprintf(buf, "%s%d%c:%s", devname, unit, 'a' + partition, file);
		return buf;
	}
	return "(invalid)";
}

static void
clearit(void)
{

	if (bootconf.clear)
		clear_pc_screen();
}

void
bootit(const char *filename, int howto, int tell)
{

	if (tell) {
		printf("booting %s", sprint_bootsel(filename));
		if (howto)
			printf(" (howto 0x%x)", howto);
		printf("\n");
	}

	if (exec_netbsd(filename, 0, howto, boot_biosdev < 0x80, clearit) < 0)
		printf("boot: %s: %s\n", sprint_bootsel(filename),
		       strerror(errno));
	else
		printf("boot returned\n");
}

void
print_banner(void)
{

	clearit();
#ifndef SMALL
	int n;
	if (bootconf.banner[0]) {
		for (n = 0; bootconf.banner[n] && n < MAXBANNER; n++) 
			printf("%s\n", bootconf.banner[n]);
	} else {
#endif /* !SMALL */
		printf("\n"
		       ">> %s, Revision %s (from NetBSD %s)\n"
		       ">> Memory: %d/%d k\n",
		       bootprog_name, bootprog_rev, bootprog_kernrev,
		       getbasemem(), getextmem());

#ifndef SMALL
	}
#endif /* !SMALL */
}

/*
 * Called from the initial entry point boot_start in biosboot.S
 *
 * biosdev: BIOS drive number the system booted from
 * biossector: Sector number of the NetBSD partition
 */
void
boot2(int biosdev, uint64_t biossector)
{
	extern char twiddle_toggle;
	int currname;
	char c;

	twiddle_toggle = 1;	/* no twiddling until we're ready */

	initio(boot_params.bp_consdev);

#ifdef SUPPORT_PS2
	biosmca();
#endif
	gateA20();

	boot_modules_enabled = !(boot_params.bp_flags
				 & X86_BP_FLAGS_NOMODULES);
	if (boot_params.bp_flags & X86_BP_FLAGS_RESET_VIDEO)
		biosvideomode();

	vbe_init();

	/* need to remember these */
	boot_biosdev = biosdev;
	boot_biossector = biossector;

	/* try to set default device to what BIOS tells us */
	bios2dev(biosdev, biossector, &default_devname, &default_unit,
		 &default_partition);

	/* if the user types "boot" without filename */
	default_filename = DEFFILENAME;

#ifndef SMALL
	if (!(boot_params.bp_flags & X86_BP_FLAGS_NOBOOTCONF)) {
		parsebootconf(BOOTCONF);
	} else {
		bootconf.timeout = boot_params.bp_timeout;
	}
	

	/*
	 * If console set in boot.cfg, switch to it.
	 * This will print the banner, so we don't need to explicitly do it
	 */
	if (bootconf.consdev)
		command_consdev(bootconf.consdev);
	else 
		print_banner();

	/* Display the menu, if applicable */
	twiddle_toggle = 0;
	if (bootconf.nummenu > 0) {
		/* Does not return */
		doboottypemenu();
	}

#else
	twiddle_toggle = 0;
	print_banner();
#endif

	printf("Press return to boot now, any other key for boot menu\n");
	for (currname = 0; currname < NUMNAMES; currname++) {
		printf("booting %s - starting in ",
		       sprint_bootsel(names[currname][0]));

#ifdef SMALL
		c = awaitkey(boot_params.bp_timeout, 1);
#else
		c = awaitkey((bootconf.timeout < 0) ? 0 : bootconf.timeout, 1);
#endif
		if ((c != '\r') && (c != '\n') && (c != '\0')) {
		    if ((boot_params.bp_flags & X86_BP_FLAGS_PASSWORD) == 0) {
			/* do NOT ask for password */
			bootmenu(); /* does not return */
		    } else {
			/* DO ask for password */
			if (check_password((char *)boot_params.bp_password)) {
			    /* password ok */
			    printf("type \"?\" or \"help\" for help.\n");
			    bootmenu(); /* does not return */
			} else {
			    /* bad password */
			    printf("Wrong password.\n");
			    currname = 0;
			    continue;
			}
		    }
		}

		/*
		 * try pairs of names[] entries, foo and foo.gz
		 */
		/* don't print "booting..." again */
		bootit(names[currname][0], 0, 0);
		/* since it failed, try compressed bootfile. */
		bootit(names[currname][1], 0, 1);
	}

	bootmenu();	/* does not return */
}

/* ARGSUSED */
void
command_help(char *arg)
{

	printf("commands are:\n"
	       "boot [xdNx:][filename] [-12acdqsvxz]\n"
	       "     (ex. \"hd0a:netbsd.old -s\"\n"
	       "ls [path]\n"
	       "dev xd[N[x]]:\n"
	       "consdev {pc|com[0123]|com[0123]kbd|auto}\n"
	       "vesa {modenum|on|off|enabled|disabled|list}\n"
	       "modules {on|off|enabled|disabled}\n"
	       "load {path_to_module}\n"
	       "multiboot [xdNx:][filename] [<args>]\n"
	       "userconf {command}\n"
	       "rndseed {path_to_rndseed_file}\n"
#if 1	/* XXX: XXXNONAKA start */
	       "mrd {address} [length]\n"
	       "mwd {address} {value}\n"
	       "pcicfgr {bus} {device} {function}\n"
	       "pcicfgw {bus} {device} {function} {address} {value}\n"
#endif	/* XXX: XXXNONAKA start */
	       "help|?\n"
	       "quit\n");
}

void
command_ls(char *arg)
{
	const char *save = default_filename;

	default_filename = "/";
	ls(arg);
	default_filename = save;
}

/* ARGSUSED */
void
command_quit(char *arg)
{

	printf("Exiting...\n");
	delay(1000000);
	reboot();
	/* Note: we shouldn't get to this point! */
	panic("Could not reboot!");
}

void
command_boot(char *arg)
{
	char *filename;
	int howto, tell;

	if (!parseboot(arg, &filename, &howto))
		return;

	tell = ((howto & AB_VERBOSE) != 0);
	if (filename != NULL) {
		bootit(filename, howto, tell);
	} else {
		int i;
		for (i = 0; i < NUMNAMES; i++) {
			bootit(names[i][0], howto, tell);
			bootit(names[i][1], howto, tell);
		}
	}
}

void
command_dev(char *arg)
{
	static char savedevname[MAXDEVNAME + 1];
	char *fsname, *devname;
	const char *file; /* dummy */

	if (*arg == '\0') {
		biosdisk_probe();
		printf("default %s%d%c\n", default_devname, default_unit,
		       'a' + default_partition);
		return;
	}

	if (strchr(arg, ':') == NULL ||
	    parsebootfile(arg, &fsname, &devname, &default_unit,
			  &default_partition, &file)) {
		command_help(NULL);
		return;
	}

	/* put to own static storage */
	strncpy(savedevname, devname, MAXDEVNAME + 1);
	default_devname = savedevname;
}

static const struct cons_devs {
	const char	*name;
	u_int		tag;
} cons_devs[] = {
	{ "pc",		CONSDEV_PC },
	{ "com0",	CONSDEV_COM0 },
	{ "com1",	CONSDEV_COM1 },
	{ "com2",	CONSDEV_COM2 },
	{ "com3",	CONSDEV_COM3 },
	{ "com0kbd",	CONSDEV_COM0KBD },
	{ "com1kbd",	CONSDEV_COM1KBD },
	{ "com2kbd",	CONSDEV_COM2KBD },
	{ "com3kbd",	CONSDEV_COM3KBD },
	{ "auto",	CONSDEV_AUTO },
	{ NULL,		0 }
};

void
command_consdev(char *arg)
{
	const struct cons_devs *cdp;

	for (cdp = cons_devs; cdp->name; cdp++) {
		if (strcmp(arg, cdp->name) == 0) {
			initio(cdp->tag);
			print_banner();
			return;
		}
	}
	printf("invalid console device.\n");
}

void
command_modules(char *arg)
{

	if (strcmp(arg, "enabled") == 0 ||
	    strcmp(arg, "on") == 0)
		boot_modules_enabled = true;
	else if (strcmp(arg, "disabled") == 0 ||
	    strcmp(arg, "off") == 0)
		boot_modules_enabled = false;
	else
		printf("invalid flag, must be 'enabled' or 'disabled'.\n");
}

void
command_multiboot(char *arg)
{
	char *filename;

	filename = arg;
	if (exec_multiboot(filename, gettrailer(arg)) < 0)
		printf("multiboot: %s: %s\n", sprint_bootsel(filename),
		       strerror(errno));
	else
		printf("boot returned\n");
}

#if 1	/* XXX: XXXNONAKA start */
static void
hexstr(char *p, uint32_t value)
{
	int i;

	for (i = 0; i < 8; i++)
		p[i] = hexdigits[(value >> (4 * (7 - i))) & 0xf];
	p[8] = '\0';
}

static int
decode_4(uint32_t *p, char *buf, size_t len)
{
	uint32_t value = 0;
	int hexoffset = 2;
	int i, c;

	if (len == 0)
		return -1;

	if (len >= 3 && buf[0] == '0' && buf[1] == 'x') {
 hex:
 		if (len > 8 + hexoffset)
			goto error;

 		for (i = hexoffset; i < len; i++) {
			value <<= 4;
			c = buf[i];
			if (c >= '0' && c <= '9') {
				value += c - '0';
			} else if (c >= 'A' && c <= 'F') {
				value += c - 'A' + 10;
			} else if (c >= 'a' && c <= 'f') {
				value += c - 'a' + 10;
			} else
				goto error;
		}
	} else if (len <= 10) {
		for (i = 0; i < len; i++) {
			c = buf[i];
			if (c >= '0' && c <= '9') {
				value *= 10;
				value += c - '0';
			} else if ((c >= 'A' && c <= 'F') ||
			    (c >= 'a' && c <= 'f')) {
				/* switch to hex decoding */
				hexoffset = 0;
				goto hex;
			} else
				goto error;
		}
	} else
		goto error;

	*p = value;
	return 0;

 error:
	return -1;
}

static int
scan(char **top, char **next, char *p, bool iserror)
{

	/* skip space and tab */
	for (;; p++) {
		if (*p == '\0')
			return -1;
		if (*p != ' ' && *p != '\t')
			break;
	}

	*top = p;
	for (;; p++) {
		if (*p == '\0') {
			if (iserror)
				return -1;
			p = NULL;
			break;
		}
		if (*p == ' ' || *p == '\t') {
			*p++ = '\0';
			break;
		}
	}
	if (next != NULL)
		*next = p;
	return 0;
}

void
command_memoryread_4(char *arg)
{
	char hex[9];
	char *p = arg;
	volatile uint32_t *mem;
	char *addressp, *lenp;
	uint32_t address, len;
	int rv;
	int i;

	rv = scan(&addressp, &p, p, false);
	if (rv != 0)
		goto error;
	rv = decode_4(&address, addressp, strlen(addressp));
	if (rv != 0)
		goto error;
	address = address & ~3;

	if (p != NULL) {
		rv = scan(&lenp, NULL, p, false);
		if (rv != 0)
			goto error;
		rv = decode_4(&len, lenp, strlen(lenp));
		if (rv != 0)
			goto error;
		len = (len + 3) & ~3;
	} else
		len = 4;

#if 1	/* XXX: va:0x00000000 <-> pa:0x00010000(SECONDARY_LOAD_ADDRESS) */
	address -= 0x10000;
#endif
	mem = (volatile uint32_t *)address;
	for (len /= 4, i = 0; len > 0; mem++, len--, i++) {
		if ((i % 4) == 0) {
			hexstr(hex, (uint32_t)mem);
			printf("%s%s:", (i != 0) ? "\n" : "", hex);
		}
		hexstr(hex, *mem);
		printf(" %s", hex);
	}
	printf("\n");
	return;

 error:
 	printf("invalid argument: <address> {length}\n");
	printf("  ex. mrd 0xfe567890 256\n");
}

void
command_memorywrite_4(char *arg)
{
	char hex[9];
	char *p = arg;
	volatile uint32_t *mem;
	char *addressp, *valuep;
	uint32_t address, value;
	int rv;

	rv = scan(&addressp, &p, p, true);
	if (rv != 0)
		goto error;
	rv = decode_4(&address, addressp, strlen(addressp));
	if (rv != 0)
		goto error;

	rv = scan(&valuep, NULL, p, false);
	if (rv != 0)
		goto error;
	rv = decode_4(&value, valuep, strlen(valuep));
	if (rv != 0)
		goto error;

#if 1	/* XXX: va:0x00000000 <-> pa:0x00010000(SECONDARY_LOAD_ADDRESS) */
	address -= 0x10000;
#endif
	mem = (volatile uint32_t *)address;

	hexstr(hex, address);
	printf("%s: ", hex);
	hexstr(hex, *mem);
	printf("%s -> ", hex);

	*mem = value;

	hexstr(hex, *mem);
	printf("%s\n", hex);

	if (*mem != value)
		printf("write failed\n");
	return;

 error:
	printf("invalid argument: <address> <value>\n");
	printf("  ex. mwd 0x01abcdef 1\n");
}

#include "cpufunc.h"

#define	PCI_MODE1_ENABLE	0x80000000U
#define	PCI_MODE1_ADDRESS_REG	0x0cf8
#define	PCI_MODE1_DATA_REG	0x0cfc

/* XXX: mode 1 only */
static int pci_mode = -1;

static int
pci_mode_detect(void)
{
	uint32_t sav, val;

	if (pci_mode != -1)
		return (pci_mode);

	sav = inl(PCI_MODE1_ADDRESS_REG);

	pci_mode = 1;	/* assume this for now */

	outl(PCI_MODE1_ADDRESS_REG, PCI_MODE1_ENABLE);
	outb(PCI_MODE1_ADDRESS_REG + 3, 0);
	outw(PCI_MODE1_ADDRESS_REG + 2, 0);
	val = inl(PCI_MODE1_ADDRESS_REG);
	if ((val & 0x80fffffc) != PCI_MODE1_ENABLE)
		goto not1;
	outl(PCI_MODE1_ADDRESS_REG, 0);
	val = inl(PCI_MODE1_ADDRESS_REG);
	if ((val & 0x80fffffc) != 0)
		goto not1;
	return (pci_mode);

 not1:
 	outl(PCI_MODE1_ADDRESS_REG, sav);
	return (pci_mode = 0);
}

static uint32_t
pci_make_tag(int bus, int device, int function)
{
	uint32_t tag;

	tag = PCI_MODE1_ENABLE;
	tag |= bus << 16;
	tag |= device << 11;
	tag |= function << 8;

	return tag;
}

void
command_dump_pcicfg(char *arg)
{
	char hex[9];
	char *p = arg;
	char *busp, *devp, *funcp;
	uint32_t bus, device, function, address;
	uint32_t tag, value;
	int rv;

	if (pci_mode_detect() != 1) {
		printf("pci mode != 1\n");
		return;
	}

	/* bus */
	rv = scan(&busp, &p, p, true);
	if (rv != 0)
		goto error;
	rv = decode_4(&bus, busp, strlen(busp));
	if (rv != 0)
		goto error;
	if (bus < 0 || bus >= 256)
		goto error;

	/* device */
	rv = scan(&devp, &p, p, true);
	if (rv != 0)
		goto error;
	rv = decode_4(&device, devp, strlen(devp));
	if (rv != 0)
		goto error;
	if (device < 0 || device >= 32)
		goto error;

	/* function */
	rv = scan(&funcp, NULL, p, false);
	if (rv != 0)
		goto error;
	rv = decode_4(&function, funcp, strlen(funcp));
	if (rv != 0)
		goto error;
	if (function < 0 || function >= 8)
		goto error;

	printf("bus %d, device %d, function %d:", bus, device, function);
	for (address = 0; address < 256; address += 4) {
		if ((address % 16) == 0) {
			hexstr(hex, address);
			printf("\n%s:", hex);
		}

		tag = pci_make_tag(bus, device, function);
		outl(PCI_MODE1_ADDRESS_REG, tag | address);
		value = inl(PCI_MODE1_DATA_REG);
		outl(PCI_MODE1_ADDRESS_REG, 0);

		hexstr(hex, value);
		printf(" %s", hex);
	}
	printf("\n");

	return;

 error:
	printf("invalid argument: <bus> <device> <function>\n");
	printf("  bus: 0-255, device: 0-31, function: 0-7\n");
}

void
command_write_pcicfg(char *arg)
{
	char hex[9];
	char *p = arg;
	char *busp, *devp, *funcp, *addressp, *valuep;
	uint32_t bus, device, function, address;
	uint32_t tag, reg, value;
	int rv;

	if (pci_mode_detect() != 1) {
		printf("pci mode != 1\n");
		return;
	}

	/* bus */
	rv = scan(&busp, &p, p, true);
	if (rv != 0)
		goto error;
	rv = decode_4(&bus, busp, strlen(busp));
	if (rv != 0)
		goto error;
	if (bus < 0 || bus >= 256)
		goto error;

	/* device */
	rv = scan(&devp, &p, p, true);
	if (rv != 0)
		goto error;
	rv = decode_4(&device, devp, strlen(devp));
	if (rv != 0)
		goto error;
	if (device < 0 || device >= 32)
		goto error;

	/* function */
	rv = scan(&funcp, &p, p, true);
	if (rv != 0)
		goto error;
	rv = decode_4(&function, funcp, strlen(funcp));
	if (rv != 0)
		goto error;
	if (function < 0 || function >= 8)
		goto error;

	/* address */
	rv = scan(&addressp, &p, p, true);
	if (rv != 0)
		goto error;
	rv = decode_4(&address, addressp, strlen(addressp));
	if (rv != 0)
		goto error;
	if (address < 0 || address >= 256 || (address & 3) != 0)
		goto error;

	/* value */
	rv = scan(&valuep, NULL, p, false);
	if (rv != 0)
		goto error;
	rv = decode_4(&value, valuep, strlen(valuep));
	if (rv != 0)
		goto error;

	printf("bus %d, device %d, function %d: ", bus, device, function);
	hexstr(hex, address);
	printf("%s: ", hex);

	tag = pci_make_tag(bus, device, function);
	outl(PCI_MODE1_ADDRESS_REG, tag | address);
	reg = inl(PCI_MODE1_DATA_REG);
	outl(PCI_MODE1_ADDRESS_REG, 0);
	hexstr(hex, reg);
	printf("%s -> ", hex);

	outl(PCI_MODE1_ADDRESS_REG, tag | address);
	outl(PCI_MODE1_DATA_REG, value);
	outl(PCI_MODE1_ADDRESS_REG, 0);

	outl(PCI_MODE1_ADDRESS_REG, tag | address);
	reg = inl(PCI_MODE1_DATA_REG);
	outl(PCI_MODE1_ADDRESS_REG, 0);
	hexstr(hex, reg);
	printf("%s\n", hex);
	return;

 error:
	printf("invalid argument: <bus> <device> <function> <address> <value>\n");
	printf("  bus: 0-255, device: 0-31, function: 0-7, address: 0-255\n");
}
#endif	/* XXX: XXXNONAKA end */
