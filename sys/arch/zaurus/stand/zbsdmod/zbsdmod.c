/*	$NetBSD: zbsdmod.c,v 1.8 2011/12/16 14:17:41 nonaka Exp $	*/
/*	$OpenBSD: zbsdmod.c,v 1.7 2005/05/02 02:45:29 uwe Exp $	*/

/*
 * Copyright (c) 2005 Uwe Stuehler <uwe@bsdx.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Copyright (C) 2006-2012 NONAKA Kimihiro <nonaka@netbsd.org>
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

/*
 * Zaurus NetBSD bootstrap loader.
 */

#include <sys/cdefs.h>
#define ELFSIZE 32
#include <sys/exec_elf.h>
#include <sys/types.h>
#include <sys/errno.h>

#include <arm/armreg.h>

#include <machine/bootinfo.h>

#include "compat_linux.h"

/* Linux LKM support */
const char __module_kernel_version[] __attribute__((section(".modinfo"))) =
    "kernel_version=" UTS_RELEASE;
const char __module_using_checksums[] __attribute__((section(".modinfo"))) =
    "using_checksums=1";

#define ZBOOTDEV_MAJOR	99
#define ZBOOTDEV_MODE	0222
#define ZBOOTDEV_NAME	"zboot"
#define ZBOOTMOD_NAME	"zbsdmod"

/* Prototypes */
int	init_module(void);
void	cleanup_module(void);

static ssize_t	zbsdmod_write(struct file *, const char *, size_t, loff_t *);
static int	zbsdmod_open(struct inode *, struct file *);
static int	zbsdmod_close(struct inode *, struct file *);

static void	elf32bsdboot(void);

static struct file_operations fops = {
	0,			/* struct module *owner */
	0,			/* lseek */
	0,			/* read */
	zbsdmod_write,		/* write */
	0,			/* readdir */
	0,			/* poll */
	0,			/* ioctl */
	0,			/* mmap */
	zbsdmod_open,		/* open */
	0,			/* flush */
	zbsdmod_close,		/* release */
	0,			/* sync */
	0,			/* async */
	0,			/* check media change */
	0,			/* revalidate */
	0,			/* lock */
	0,			/* sendpage */
	0,			/* get_unmapped_area */
#ifdef	MAGIC_ROM_PTR
	0,			/* romptr */
#endif	/* MAGIC_ROM_PTR */
};

/* driver status */
static int isopen;
static loff_t position;

/* bootinfo */
static union {
	struct {
		u_int magic;
		char info[BOOTARGS_BUFSIZ - sizeof(u_int)];
	} args __packed;
	char buf[BOOTARGS_BUFSIZ];
} bootargs;

/* D-cache cleaning region */
static u_int datacacheclean[65536/sizeof(u_int)] __aligned(32);

/*
 * page tag operations
 */
struct zbsdmod_page_tag {
	struct zbsdmod_page_tag *next;
	char *ptr;
	uint32_t off;
	uint32_t no;
	uint32_t sz;
} __packed __aligned(4);

static struct zbsdmod_page_tag *page_tag_top;

#define	ALLOC_SIZE	(4 * 1024)	/* PAGE_SIZE */
#define	BUCKET_SIZE	(ALLOC_SIZE - sizeof(struct zbsdmod_page_tag))
#define	ALLOC_FLAGS	(GFP_KERNEL)

static inline struct zbsdmod_page_tag *
zbsdmod_get_tag(uint32_t pos, int alloc)
{
	static struct zbsdmod_page_tag *tag;
	static uint32_t pageno;

	pageno = 0;
	while (pos >= BUCKET_SIZE) {
		pos -= BUCKET_SIZE;
		pageno++;
	}
	for (tag = page_tag_top; tag != NULL; tag = tag->next) {
		if (pageno == tag->no) {
			tag->off = pos;
			return tag;
		}
	}

	if (!alloc)
		return NULL;

	tag = kmalloc(ALLOC_SIZE, ALLOC_FLAGS);
	if (tag == NULL)
		return NULL;
	memset(tag, 0, ALLOC_SIZE);

	tag->ptr = (char *)tag + sizeof(struct zbsdmod_page_tag);
	tag->no = pageno;
	tag->sz = 0;
	tag->next = page_tag_top;
	page_tag_top = tag;

	tag->off = pos;
	return tag;
}

static int
zbsdmod_page_copy(uint32_t dst, const void *src, uint32_t size)
{
	struct zbsdmod_page_tag *tag;
	size_t freesz;

	while (size > 0) {
		tag = zbsdmod_get_tag(dst, 1);
		if (tag == NULL)
			return ENOMEM;

		freesz = BUCKET_SIZE - tag->off;
		if (freesz > size)
			freesz = size;

		if (tag->sz < tag->off + freesz)
			tag->sz = tag->off + freesz;

		size -= freesz;
		dst += freesz;

		memcpy(tag->ptr + tag->off, src, freesz);
		src = (const char *)src + freesz;
	}
	return 0;
}

static inline int
zbsdmod_page_read(void *dst, uint32_t src, size_t size)
{
	static struct zbsdmod_page_tag *tag;
	static size_t freesz;
	static char *cpdst;

	cpdst = dst;
	while (size > 0) {
		tag = zbsdmod_get_tag(src, 0);
		if (tag == NULL)
			return EINVAL;

		freesz = BUCKET_SIZE - tag->off;
		if (freesz > size)
			freesz = size;

		if ((tag->sz < tag->off) || (freesz > tag->sz - tag->off))
			return EINVAL;

		size -= freesz;
		src += freesz;

		/* don't use memcpy!!! */
		while (freesz > 0) {
			freesz--;
			*cpdst++ = tag->ptr[tag->off++];
		}
	}
	return 0;
}

static void
zbsdmod_page_tag_cleanup(void)
{
	struct zbsdmod_page_tag *tag, *next;

	for (tag = page_tag_top; tag != NULL; tag = next) {
		next = tag->next;
		kfree(tag);
	}
	page_tag_top = NULL;
}

/*
 * Boot the loaded BSD kernel image, or return if an error is found.
 * Part of this routine is borrowed from sys/lib/libsa/loadfile.c.
 */
static void
elf32bsdboot(void)
{
	static vaddr_t minv, maxv, posv;
	static vaddr_t ehv, shdrv;
	static vaddr_t *esymp;
	static Elf_Ehdr eh;
	static Elf_Phdr *phdr;
	static Elf_Shdr *shdr;
	static Elf_Off off;
	static int havesyms;
	static int cpsr, ocpsr;
	static u_int sz;
	static const char *cpsrc;
	static char *cpdst;
	static int i;
	static int rv;

	rv = zbsdmod_page_read(&eh, 0, sizeof(eh));
	if (rv) {
		printk("%s: couldn't read Elf_Ehdr. rv=%d\n", __func__, rv);
		return;
	}
	if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
	    eh.e_ident[EI_CLASS] != ELFCLASS32)
		return;

	minv = (vaddr_t)~0;
	maxv = (vaddr_t)0;
	posv = (vaddr_t)0;
	esymp = NULL;

	/*
	 * Get min and max addresses used by the loaded kernel.
	 */
	sz = eh.e_phnum * eh.e_phentsize;
	phdr = kmalloc(sz, ALLOC_FLAGS);
	if (phdr == NULL) {
		printk("%s: couldn't alloc memory(Elf_Phdr)\n", __func__);
		return;
	}
	rv = zbsdmod_page_read(phdr, eh.e_phoff, sz);
	if (rv) {
		printk("%s: couldn't read Elf_Phdr. rv=%d\n", __func__, rv);
		goto free_phdr;
	}
	for (i = 0; i < eh.e_phnum; i++) {
		if (phdr[i].p_type != PT_LOAD ||
		    (phdr[i].p_flags & (PF_W|PF_R|PF_X)) == 0)
			continue;

#define IS_TEXT(p)	(p.p_flags & PF_X)
#define IS_DATA(p)	(p.p_flags & PF_W)
#define IS_BSS(p)	(p.p_filesz < p.p_memsz)
		/*
		 * XXX: Assume first address is lowest
		 */
		if (IS_TEXT(phdr[i]) || IS_DATA(phdr[i])) {
			posv = phdr[i].p_vaddr;
			if (minv > posv)
				minv = posv;
			posv += phdr[i].p_filesz;
			if (maxv < posv)
				maxv = posv;
		}
		if (IS_DATA(phdr[i]) && IS_BSS(phdr[i])) {
			posv += phdr[i].p_memsz;
			if (maxv < posv)
				maxv = posv;
		}
		/*
		 * 'esym' is the first word in the .data section,
		 * and marks the end of the symbol table.
		 */
		if (IS_DATA(phdr[i]) && !IS_BSS(phdr[i]))
			esymp = (vaddr_t *)phdr[i].p_vaddr;
	}

	/*
	 * Copy ELF section headers.
	 */
	sz = eh.e_shnum * eh.e_shentsize;
	shdr = kmalloc(sz, ALLOC_FLAGS);
	if (shdr == NULL) {
		printk("%s: couldn't alloc memory(Elf_Shdr)\n", __func__);
		goto free_phdr;
	}
	rv = zbsdmod_page_read(shdr, eh.e_shoff, sz);
	if (rv) {
		printk("%s: couldn't read Elf_Shdr. rv=%d\n", __func__, rv);
		goto free_shp;
	}

	/* Disable interrupt. */
	__asm volatile ("mrs %0, cpsr_all" : "=r" (ocpsr) :: "memory");
	cpsr = ocpsr | IF32_bits;
	__asm volatile ("msr cpsr_all, %0" :: "r" (cpsr) : "memory");

	/*
	 * Copy the boot arguments.
	 */
	sz = BOOTARGS_BUFSIZ;
	cpsrc = bootargs.buf;
	cpdst = (char *)(minv - BOOTARGS_BUFSIZ);
	while (sz > 0) {
		sz--;
		*cpdst++ = *cpsrc++;
	}

	/*
	 * Set up pointers to copied ELF and section headers.
	 */
#define roundup(x, y)	((((x)+((y)-1))/(y))*(y))
	ehv = maxv = roundup(maxv, sizeof(long));
	maxv += sizeof(Elf_Ehdr);
	sz = eh.e_shnum * eh.e_shentsize;
	shdrv = maxv;
	maxv += roundup(sz, sizeof(long));

	/*
	 * Now load the symbol sections themselves.  Make sure the
	 * sections are aligned, and offsets are relative to the
	 * copied ELF header.  Don't bother with string tables if
	 * there are no symbol sections.
	 */
	off = roundup((sizeof(Elf_Ehdr) + sz), sizeof(long));
	havesyms = 0;
	for (i = 0; i < eh.e_shnum; i++) {
		if (shdr[i].sh_type == SHT_SYMTAB) {
			havesyms = 1;
			break;
		}
	}
	for (i = 0; i < eh.e_shnum; i++) {
		if (shdr[i].sh_type == SHT_SYMTAB ||
		    shdr[i].sh_type == SHT_STRTAB) {
			if (havesyms) {
				(void)zbsdmod_page_read((void *)maxv,
				    shdr[i].sh_offset, shdr[i].sh_size);
			}
			maxv += roundup(shdr[i].sh_size, sizeof(long));
			shdr[i].sh_offset = off;
			off += roundup(shdr[i].sh_size, sizeof(long));
		}
	}

	/*
	 * Copy the ELF and section headers.
	 */
	sz = sizeof(Elf_Ehdr);
	cpsrc = (char *)&eh;
	cpdst = (char *)ehv;
	while (sz > 0) {
		sz--;
		*cpdst++ = *cpsrc++;
	}

	sz = eh.e_shnum * eh.e_shentsize;
	cpsrc = (char *)shdr;
	cpdst = (char *)shdrv;
	while (sz > 0) {
		sz--;
		*cpdst++ = *cpsrc++;
	}

	/*
	 * Frob the copied ELF header to give information relative to ehv.
	 */
	((Elf_Ehdr *)ehv)->e_phoff = 0;
	((Elf_Ehdr *)ehv)->e_shoff = sizeof(Elf_Ehdr);
	((Elf_Ehdr *)ehv)->e_phentsize = 0;
	((Elf_Ehdr *)ehv)->e_phnum = 0;

	/*
	 * Tell locore.S where the symbol table ends, and arrange
	 * to skip esym when loading the data section.
	 */
	if (esymp != NULL) {
		*esymp = (vaddr_t)maxv;
		for (i = 0; i < eh.e_phnum; i++) {
			if (phdr[i].p_type != PT_LOAD ||
			    (phdr[i].p_flags & (PF_W|PF_R|PF_X)) == 0)
				continue;

			if (phdr[i].p_vaddr == (vaddr_t)esymp) {
				phdr[i].p_vaddr += sizeof(long);
				phdr[i].p_offset += sizeof(long);
				phdr[i].p_filesz -= sizeof(long);
				break;
			}
		}
	}

	/*
	 * Load text and data.
	 */
	for (i = 0; i < eh.e_phnum; i++) {
		if (phdr[i].p_type != PT_LOAD ||
		    (phdr[i].p_flags & (PF_W|PF_R|PF_X)) == 0)
			continue;

		if (IS_TEXT(phdr[i]) || IS_DATA(phdr[i])) {
			(void)zbsdmod_page_read((void *)phdr[i].p_vaddr,
			    phdr[i].p_offset, phdr[i].p_filesz);
		}
	}

	__asm volatile (
		/* Clean D-cache */
		"mov	r0, %1;"
		"mov	r1, #65536;"
		"1:"
		"ldr	r2, [r0], #32;"
		"subs	r1, r1, #32;"
		"bne	1b;"
		"mcr	p15, 0, r1, c7, c10, 4;" /*drain write and fill buffer*/
		"mrc	p15, 0, r1, c2, c0, 0;" /* CPWAIT */
		"mov	r1, r1;"
		"sub	pc, pc, #4;"
		/* Disable MMU and jump to kernel entry address */
		"mov	r0, %0;"
		"mcr	p15, 0, r1, c7, c7, 0;" /* flush I+D cache */
		"mrc	p15, 0, r1, c2, c0, 0;" /* CPWAIT */
		"mov	r1, r1;"
		"sub	pc, pc, #4;"
		"mov	r1, #(0x10|0x20);"	/* CPU_CONTROL_32BD|BP_ENABLE */
		"mcr	p15, 0, r1, c1, c0, 0;" /* Write new control register */
		"mcr	p15, 0, r1, c8, c7, 0;" /* invalidate I+D TLB */
		"mcr	p15, 0, r1, c7, c5, 0;" /* invalidate I$ and BTB */
		"mcr	p15, 0, r1, c7, c10, 4;" /*drain write and fill buffer*/
		"mrc	p15, 0, r1, c2, c0, 0;" /* CPWAIT_AND_RETURN */
		"sub	pc, r0, r1, lsr #32;"
		: /* Nothing */
		: "r" (eh.e_entry), "r" (datacacheclean)
		: "r0", "r1", "r2", "memory");

free_shp:
	kfree(shdr);
free_phdr:
	kfree(phdr);
}

/*
 * Initialize the module.
 */
int
init_module(void)
{
	struct proc_dir_entry *entry;
	int error;

	error = register_chrdev(ZBOOTDEV_MAJOR, ZBOOTDEV_NAME, &fops);
	if (error) {
		printk("%s: register_chrdev(%d, ...): error %d\n",
		    ZBOOTMOD_NAME, ZBOOTDEV_MAJOR, -error);
		return 1;
	}

	entry = proc_mknod(ZBOOTDEV_NAME, ZBOOTDEV_MODE | S_IFCHR,
	    &proc_root, MKDEV(ZBOOTDEV_MAJOR, 0));
	if (entry == NULL) {
		(void) unregister_chrdev(ZBOOTDEV_MAJOR, ZBOOTDEV_NAME);
		return 1;
	}

	printk("%s: NetBSD/" MACHINE " bootstrap device is %d,0\n",
	    ZBOOTMOD_NAME, ZBOOTDEV_MAJOR);
	return 0;
}

/*
 * Cleanup - undo whatever init_module did.
 */
void
cleanup_module(void)
{

	(void) unregister_chrdev(ZBOOTDEV_MAJOR, ZBOOTDEV_NAME);
	remove_proc_entry(ZBOOTDEV_NAME, &proc_root);

	zbsdmod_page_tag_cleanup();

	printk("%s: NetBSD/" MACHINE " bootstrap device unloaded\n",
	    ZBOOTMOD_NAME);
}

static ssize_t
zbsdmod_write(struct file *f, const char *buf, size_t len, loff_t *offp)
{
	int error;

	if (len < 1)
		return 0;

	error = zbsdmod_page_copy(*offp, buf, len);
	if (error) {
		position = 0;
		return -error;
	}

	*offp += len;
	if (*offp > position)
		position = *offp;

	return len;
}

static int
zbsdmod_open(struct inode *ino, struct file *f)
{

	/* XXX superuser check */

	if (isopen)
		return -EBUSY;

	isopen = 1;
	position = 0;

	return 0;
}

static int
zbsdmod_close(struct inode *ino, struct file *f)
{

	if (!isopen)
		return -EBUSY;

	if (position > 0) {
		printk("%s: loaded %ld bytes\n", ZBOOTDEV_NAME, position);
		if (position < (loff_t)BOOTINFO_MAXSIZE) {
			bootargs.args.magic = BOOTARGS_MAGIC;
			zbsdmod_page_read(bootargs.args.info, 0, position);
		} else {
			elf32bsdboot();
			printk("%s: boot failed\n", ZBOOTDEV_NAME);
		}
	}
	zbsdmod_page_tag_cleanup();
	isopen = 0;

	return 0;
}
