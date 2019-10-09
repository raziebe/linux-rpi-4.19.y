/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <asm/cacheflush.h>
#include <linux/mman.h>

#include <linux/io.h>
#include <linux/hugetlb.h>
#include <linux/sched/signal.h>
#include <asm/pgalloc.h>

#include <asm/virt.h>
#include <asm/system_misc.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/ioctl.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>

#include <linux/mman.h>
#include <linux/io.h>
#include <linux/hugetlb.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <linux/hyplet.h>
#include <linux/highmem.h>

#include "hyp_mmu.h"


extern char  __hyp_idmap_text_start[], __hyp_idmap_text_end[];
extern char __hyplet_init_vec[];

static pgd_t *boot_hyp_pgd;
pgd_t *hyp_pgd;
static pgd_t *merged_hyp_pgd;
static DEFINE_MUTEX(hyplet_mutex);

static unsigned long hyp_idmap_start;
static unsigned long hyp_idmap_end;
static phys_addr_t hyp_idmap_vector;

#define hyp_pud_addr_end(addr, end)     pud_addr_end(addr, end)

static void clear_pgd_entry(pgd_t *pgd, phys_addr_t addr)
{
	pud_t *pud_table __maybe_unused = pud_offset(pgd, 0);
	pgd_clear(pgd);
	pud_free(NULL, pud_table);
	put_page(virt_to_page(pgd));
}

static void clear_pud_entry(pud_t *pud, phys_addr_t addr)
{
	pmd_t *pmd_table = pmd_offset(pud, 0);
	VM_BUG_ON(pud_huge(*pud));
	pud_clear(pud);
	pmd_free(NULL, pmd_table);
	put_page(virt_to_page(pud));
}

static void clear_pmd_entry(pmd_t *pmd, phys_addr_t addr)
{
	pte_t *pte_table = pte_offset_kernel(pmd, 0);
	VM_BUG_ON(hyp_pmd_huge(*pmd));
	pmd_clear(pmd);
	pte_free_kernel(NULL, pte_table);
	put_page(virt_to_page(pmd));
}


static void unmap_ptes(pmd_t *pmd,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t start_addr = addr;
	pte_t *pte, *start_pte;

	start_pte = pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_none(*pte)) {
			pte_t old_pte = *pte;

			hyp_set_pte(pte, __pte(0));
			/* No need to invalidate the cache for device mappings */
			if (pfn_valid(pte_pfn(old_pte)))
				hyp_flush_dcache_pte(old_pte);
			put_page(virt_to_page(pte));
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);

	if (hyp_pte_table_empty(start_pte))
		clear_pmd_entry(pmd, start_addr);
}

static void unmap_pmds(pud_t *pud,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next, start_addr = addr;
	pmd_t *pmd, *start_pmd;

	start_pmd = pmd = pmd_offset(pud, addr);
	do {
		next = hyp_pmd_addr_end(addr, end);
		if (!pmd_none(*pmd)) {
			if (hyp_pmd_huge(*pmd)) {
				pmd_t old_pmd = *pmd;
				pmd_clear(pmd);
				hyp_flush_dcache_pmd(old_pmd);
				put_page(virt_to_page(pmd));
			} else {
				pmd_t old_pmd = *pmd;
				hyp_flush_dcache_pmd(old_pmd);
				unmap_ptes(pmd, addr, next);
			}
		}
	} while (pmd++, addr = next, addr != end);

	if (hyp_pmd_table_empty(start_pmd))
		clear_pud_entry( pud, start_addr);
}


static void unmap_puds( pgd_t *pgd,
		       phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t next, start_addr = addr;
	pud_t *pud, *start_pud;

	start_pud = pud = pud_offset(pgd, addr);
	do {
		next = hyp_pud_addr_end(addr, end);
		if (!pud_none(*pud)) {
			if (pud_huge(*pud)) {
				pud_t old_pud = *pud;
				pud_clear(pud);
				hyp_flush_dcache_pud(old_pud);
				put_page(virt_to_page(pud));
			} else {
				unmap_pmds( pud, addr, next);
			}
		}
	} while (pud++, addr = next, addr != end);

       if (hyp_pud_table_empty(start_pud))
                clear_pgd_entry(pgd, start_addr);

}

static void unmap_range(pgd_t *pgdp,
			phys_addr_t start, u64 size)
{
	pgd_t *pgd;
	phys_addr_t addr = start, end = start + size;
	phys_addr_t next;

	pgd = pgdp + pgd_index(addr);
	do {
		next = hyp_pgd_addr_end(addr, end);
		if (!pgd_none(*pgd))
			unmap_puds(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

/**
 * free_boot_hyp_pgd - free HYP boot page tables
 *
 * Free the HYP boot page tables. The bounce page is also freed.
 */
void free_boot_hyp_pgd(void)
{
	mutex_lock(&hyplet_mutex);

	if (boot_hyp_pgd) {
		unmap_range(boot_hyp_pgd, hyp_idmap_start, PAGE_SIZE);
		unmap_range(boot_hyp_pgd, TRAMPOLINE_VA, PAGE_SIZE);
		free_pages((unsigned long)boot_hyp_pgd, hyp_pgd_order);
		boot_hyp_pgd = NULL;
	}

	if (hyp_pgd)
		unmap_range( hyp_pgd, TRAMPOLINE_VA, PAGE_SIZE);

	mutex_unlock(&hyplet_mutex);
}

void free_hyp_pgds(void)
{
	unsigned long addr;

	free_boot_hyp_pgd();

	mutex_lock(&hyplet_mutex);

	if (hyp_pgd) {
		for (addr = PAGE_OFFSET; virt_addr_valid(addr); addr += PGDIR_SIZE)
			unmap_range( hyp_pgd, KERN_TO_HYP(addr), PGDIR_SIZE);
		for (addr = VMALLOC_START; is_vmalloc_addr((void*)addr); addr += PGDIR_SIZE)
			unmap_range( hyp_pgd, KERN_TO_HYP(addr), PGDIR_SIZE);

		free_pages((unsigned long)hyp_pgd, hyp_pgd_order);
		hyp_pgd = NULL;
	}
	if (merged_hyp_pgd) {
		clear_page(merged_hyp_pgd);
		free_page((unsigned long)merged_hyp_pgd);
		merged_hyp_pgd = NULL;
	}

	mutex_unlock(&hyplet_mutex);
}

static void create_hyp_pte_mappings(pmd_t *pmd, unsigned long start,
				    unsigned long end, unsigned long pfn,
				    pgprot_t prot)
{
	pte_t *pte;
	unsigned long addr;

	addr = start;
	do {
		pte = pte_offset_kernel(pmd, addr);
		hyp_set_pte(pte, pfn_pte(pfn, prot));
		get_page(virt_to_page(pte));
		hyp_flush_dcache_to_poc(pte, sizeof(*pte));
		pfn++;
	} while (addr += PAGE_SIZE, addr != end);
}

static int create_hyp_pmd_mappings(pud_t *pud, unsigned long start,
				   unsigned long end, unsigned long pfn,
				   pgprot_t prot)
{
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr, next;

	addr = start;
	do {
		pmd = pmd_offset(pud, addr);

		BUG_ON(pmd_sect(*pmd));

		if (pmd_none(*pmd)) {
			pte = pte_alloc_one_kernel(NULL, addr);
			if (!pte) {
				printk("Cannot allocate Hyp pte\n");
				return -ENOMEM;
			}
			pmd_populate_kernel(NULL, pmd, pte);
			get_page(virt_to_page(pmd));
			hyp_flush_dcache_to_poc(pmd, sizeof(*pmd));
		}

		next = pmd_addr_end(addr, end);

		create_hyp_pte_mappings(pmd, addr, next, pfn, prot);
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);

	return 0;
}

static int create_hyp_pud_mappings(pgd_t *pgd, unsigned long start,
				   unsigned long end, unsigned long pfn,
				   pgprot_t prot)
{
	pud_t *pud;
	pmd_t *pmd;
	unsigned long addr, next;
	int ret;

	addr = start;
	do {
		pud = pud_offset(pgd, addr);

		if (pud_none_or_clear_bad(pud)) {
			pmd = pmd_alloc_one(NULL, addr);
			if (!pmd) {
				printk("Cannot allocate Hyp pmd\n");
				return -ENOMEM;
			}
			pud_populate(NULL, pud, pmd);
			get_page(virt_to_page(pud));
			hyp_flush_dcache_to_poc(pud, sizeof(*pud));
		}

		next = pud_addr_end(addr, end);
		ret = create_hyp_pmd_mappings(pud, addr, next, pfn, prot);
		if (ret)
			return ret;
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);

	return 0;
}


void hyp_user_unmap(unsigned long umem,int size,int user)
{
       int sz_page = PAGE_ALIGN(size);
       if (user)
                unmap_range( hyp_pgd, umem, sz_page);
       else
                unmap_range( hyp_pgd, KERN_TO_HYP(umem) , sz_page);
}

int __create_hyp_mappings(pgd_t *pgdp,
			 unsigned long start, unsigned long end,
			 unsigned long pfn, pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	unsigned long addr, next;
	int err = 0;

	mutex_lock(&hyplet_mutex);
	addr = start & PAGE_MASK;
	end = PAGE_ALIGN(end);
	do {
		pgd = pgdp + pgd_index(addr);

		if (pgd_none(*pgd)) {
			pud = pud_alloc_one(NULL, addr);
			if (!pud) {
				printk("Cannot allocate Hyp pud\n");
				err = -ENOMEM;
				goto out;
			}
			pgd_populate(NULL, pgd, pud);
			get_page(virt_to_page(pgd));
			hyp_flush_dcache_to_poc(pgd, sizeof(*pgd));
		}

		next = pgd_addr_end(addr, end);
		err = create_hyp_pud_mappings(pgd, addr, next, pfn, prot);
		if (err)
			goto out;
		pfn += (next - addr) >> PAGE_SHIFT;
	} while (addr = next, addr != end);
out:
	mutex_unlock(&hyplet_mutex);
	return err;
}

phys_addr_t kaddr_to_phys(void *kaddr)
{
	if (!is_vmalloc_addr(kaddr)) {
		BUG_ON(!virt_addr_valid(kaddr));
		return __pa(kaddr);
	} else {
		return page_to_phys(vmalloc_to_page(kaddr)) +
		       offset_in_page(kaddr);
	}
}

/**
 * create_hyp_mappings - duplicate a kernel virtual address range in Hyp mode
 * @from:	The virtual kernel start address of the range
 * @to:		The virtual kernel end address of the range (exclusive)
 *
 * The same virtual address as the kernel virtual address is also used
 * in Hyp-mode mapping (modulo HYP_PAGE_OFFSET) to the same underlying
 * physical pages.
 */
int create_hyp_mappings(void *from, void *to,pgprot_t prot)
{
	phys_addr_t phys_addr;
	unsigned long virt_addr;
	unsigned long start = KERN_TO_HYP((unsigned long)from);
	unsigned long end = KERN_TO_HYP((unsigned long)to);

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);

	for (virt_addr = start; virt_addr < end; virt_addr += PAGE_SIZE) {
		int err;

		phys_addr = kaddr_to_phys(from + virt_addr - start);
		err = __create_hyp_mappings(hyp_pgd, virt_addr,
					    virt_addr + PAGE_SIZE,
					    __phys_to_pfn(phys_addr),
					    prot);
		if (err)
			return err;
	}

	return 0;
}

phys_addr_t hyp_mmu_get_httbr(void)
{
	return virt_to_phys(hyp_pgd);
}

phys_addr_t hyp_mmu_get_boot_httbr(void)
{
	return virt_to_phys(boot_hyp_pgd);
}

phys_addr_t hyp_get_idmap_vector(void)
{
	return hyp_idmap_vector;
}

int hyp_mmu_init(void)
{
	int err;

	hyp_idmap_start = virt_to_phys(__hyp_idmap_text_start);
	hyp_idmap_end = virt_to_phys(__hyp_idmap_text_end);
	hyp_idmap_vector = virt_to_phys(__hyplet_init_vec);

	/*
	 * We rely on the linker script to ensure at build time that the HYP
	 * init code does not cross a page boundary.
	 */
	BUG_ON((hyp_idmap_start ^ (hyp_idmap_end - 1)) & PAGE_MASK);

	hyp_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, hyp_pgd_order);
	boot_hyp_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, hyp_pgd_order);

	if (!hyp_pgd || !boot_hyp_pgd) {
		printk("Hyp mode PGD not allocated\n");
		err = -ENOMEM;
		goto out;
	}

	/* Create the idmap in the boot page tables */
	err = 	__create_hyp_mappings(boot_hyp_pgd,
				      hyp_idmap_start, hyp_idmap_end,
				      __phys_to_pfn(hyp_idmap_start),
				      PAGE_HYP_EXEC);

	if (err) {
		printk("Failed to idmap %lx-%lx\n",
			hyp_idmap_start, hyp_idmap_end);
		goto out;
	}

	/* Map the very same page at the trampoline VA */
	err = 	__create_hyp_mappings(boot_hyp_pgd,
				      TRAMPOLINE_VA, TRAMPOLINE_VA + PAGE_SIZE,
				      __phys_to_pfn(hyp_idmap_start),
				      PAGE_HYP_EXEC);
	if (err) {
		printk("Failed to map trampoline @%lx into boot HYP pgd\n",
			TRAMPOLINE_VA);
		goto out;
	}

	/* Map the same page again into the runtime page tables */
	err = 	__create_hyp_mappings(hyp_pgd,
				      TRAMPOLINE_VA, TRAMPOLINE_VA + PAGE_SIZE,
				      __phys_to_pfn(hyp_idmap_start),
				      PAGE_HYP_EXEC);
	if (err) {
		printk("Failed to map trampoline @%lx into runtime HYP pgd\n",
			TRAMPOLINE_VA);
		goto out;
	}

	return 0;
out:
	free_hyp_pgds();
	return err;
}
