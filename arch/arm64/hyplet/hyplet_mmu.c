#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <linux/list.h>

#include <linux/highmem.h>
#include <linux/compiler.h>


#include <linux/hyplet.h>
#include <linux/hyplet_user.h>
#include "hypletS.h"
#include "hyp_mmu.h"

/* We assume no more than 1000 io addresses are un use */


extern pgd_t *hyp_pgd;

unsigned long __hyp_text get_ioaddressesNR(void){
	struct hyplet_vm *hyp = hyplet_get_vm();
	return hyp->iomemaddr->ioaddressesNR;
}

unsigned long get_ttbr1_el1(void)
{
    u64 t;
    asm("mrs %0,ttbr1_el1" : "=r" (t));
    return t;
}

unsigned long get_tcr_el1(void)
{
    u64 t;
    asm("mrs %0,tcr_el1" : "=r" (t));
    return t;
}

unsigned int get_el1_address_size(void)
{
	long tcr_el1;
	unsigned long ips;

	tcr_el1 = get_tcr_el1();
	ips = (tcr_el1  & 0x700000000)>>32;
	switch(ips)
	{
	case 000:
		return 32; // 4GB
	case 0b001:
		return 36; // 64GB
	case 0b010:
		return 40 ;// 1TB.
	case 0b011:
		return 42;// 4TB.
	case 0b100:
		return 44;//  16TB.
	case 0b101:
		return 48; // 256TB.
	default:
		return -1;
	}
}

unsigned long get_el1_starting_address(void)
{
	long tcr_el1;
	unsigned long t1sz;

	tcr_el1 = get_tcr_el1();
	t1sz = (tcr_el1 & 0x3F0000 ) >> 16;

	return (0x1LL << (64 - t1sz));
}

int __hyp_text  is_device_mem(struct hyplet_vm *hyp,unsigned long phyaddr)
{
	int i = 0;
	struct IoMemAddr* iomemaddr = KERN_TO_HYP(hyp->iomemaddr);

	for (; i <  iomemaddr->ioaddressesNR; i++)
		if (iomemaddr->iomemaddr[i] == (phyaddr & ~0xFFF) )
				return 1;
	return 0;
}

void walk_on_mmu_el1(void)
{
	unsigned long i,j,k;
	unsigned long* pgd,*v_pgd;
	unsigned long* pmd,*v_pmd;
	unsigned long* pte;
	unsigned long temp;
	int mem_attr;
	struct hyplet_vm *hyp = hyplet_get_vm();

	// read ttbr_el1
	pgd = phys_to_virt((phys_addr_t)get_ttbr1_el1());

	for (i = 0 ; i < PAGE_SIZE/sizeof(long); i++){

		temp = pgd[i] & 0xFFFFFFFFF000LL;
		if (!temp)
			continue;

		v_pgd = (unsigned long *)phys_to_virt((phys_addr_t)temp);

		for (j = 0 , pmd = v_pgd; j < PAGE_SIZE/sizeof(long); j++){

			temp = pmd[j] & 0x0FFFFFFFFF000LL;
			if (!temp)
				continue;

			v_pmd = phys_to_virt((phys_addr_t)temp);
			if (!(pmd[j] & 0b10)) {
				// is block
				mem_attr = (pmd[j] & 0b11100)>>2;
				if (mem_attr == 1)
					printk("pmd %lx is DeviceNG\n",temp);
				continue;
			}

			for (k = 0, pte = v_pmd; k < PAGE_SIZE/sizeof(long); k++){

				temp = pte[k] & 0xFFFFFFFFF000LL;
				if (!temp)
					continue;
				mem_attr = (pte[k] & 0b11100) >> 2;
				if ((mem_attr == MT_DEVICE_nGnRnE)
						|| (mem_attr == MT_DEVICE_nGnRE)
						|| (mem_attr == MT_DEVICE_GRE) ) {

					hyp->iomemaddr->iomemaddr[hyp->iomemaddr->ioaddressesNR++] = temp;
				}
			}
		}
	}
}

unsigned long kvm_uaddr_to_pfn(unsigned long uaddr)
{
	unsigned long pfn;
	struct page *pages[1];
	int nr;

	nr = get_user_pages_fast(uaddr,1, 0, (struct page **)&pages);
	if (nr <= 0){
	     //  printk("TP: INSANE: failed to get user pages %p\n",(void *)uaddr);
	       return 0x00;
	}
	pfn = page_to_pfn(pages[0]);
	put_page(pages[0]);
	return pfn;
}

/**
 * create_hyp_user_mappings - duplicate a user virtual address range in Hyp mode
 * @from:	The virtual kernel start address of the range
 * @to:		The virtual kernel end address of the range (exclusive)
 *
 * The same virtual address as the kernel virtual address is also used
 * in Hyp-mode mapping (modulo HYP_PAGE_OFFSET) to the same underlying
 * physical pages.
 */
int create_hyp_user_mappings(void *from, void *to,pgprot_t prot)
{
	unsigned long virt_addr;
	unsigned long fr = (unsigned long)from;
	unsigned long start = USER_TO_HYP((unsigned long)from);
	unsigned long end = USER_TO_HYP((unsigned long)to);
	int mapped = 0;

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);

	for (virt_addr = start; virt_addr < end; virt_addr += PAGE_SIZE,fr += PAGE_SIZE) {
		int err;
		unsigned long pfn;

		pfn = kvm_uaddr_to_pfn(fr);
		if (pfn <= 0)
			continue;

		err = __create_hyp_mappings(hyp_pgd, virt_addr,
					    virt_addr + PAGE_SIZE,
					    pfn,
						prot);
		if (err) {
			printk("TP: Failed to map %p\n",(void *)virt_addr);
			return mapped;
		}
		mapped++;
	}

	return mapped;
}

struct hyp_addr* hyplet_get_addr_segment(long addr,struct hyplet_vm *tv)
{
	struct hyp_addr* tmp;

	if (list_empty(&tv->hyp_addr_lst))
			return NULL;

	list_for_each_entry(tmp,  &tv->hyp_addr_lst,lst) {

		long start = tmp->addr;
		long end = tmp->addr + tmp->size;

		if ( ( addr < end && addr >= start) )
			return tmp;

	}
	return NULL;
}

int __hyplet_map_user_data(long umem,int size,int flags,struct hyplet_vm *hyp)
{
	struct hyp_addr* addr;
	int pages = 0;

	pages = create_hyp_user_mappings((void *)umem, (void *)(umem + size), PAGE_HYP_RW_EXEC);
	if (pages <= 0){
		hyplet_debug(" failed to map to ttbr0_el2 size %d\n",size);
		return -1;
	}

	addr = kmalloc(sizeof(struct hyp_addr ), GFP_USER);
	addr->addr = (unsigned long)umem;
	addr->size = pages * PAGE_SIZE;
	addr->flags = flags;
	addr->nr_pages = pages;
	list_add(&addr->lst, &hyp->hyp_addr_lst);

//	hyplet_info("pid %d user mapped %lx size=%d pages=%d\n",
	//		current->pid,umem ,size, addr->nr_pages );

	if (flags & VM_EXEC) {
		hyp->state  |= USER_CODE_MAPPED;
	}
	return 0;
}

int hyplet_map_user(struct hyplet_vm *hyp,struct hyplet_ctrl *hypctl)
{
	struct vm_area_struct* vma;
	long start = hypctl->addr.addr;
	int size  = hypctl->addr.size;
	long end = start + size;

	vma = current->mm->mmap;

	for (; vma ; vma = vma->vm_next) {
		long vm_start = vma->vm_start;
		long vm_end  = vma->vm_end;

		if (vm_start <= start && vm_end >= end){
				return  __hyplet_map_user_data(start,
						size, vma->vm_flags, hyp);
		}
	}

	return -EINVAL;
}

int hyplet_map_user_vma(struct hyplet_vm *hyp,struct hyplet_ctrl *hypctl)
{
	struct vm_area_struct* vma;
	long start = hypctl->addr.addr;
	int size  = hypctl->addr.size;
	long end = start + size;

	vma = current->mm->mmap;

	for (; vma ; vma = vma->vm_next) {
		long vm_start = vma->vm_start;
		long vm_end  = vma->vm_end;

		size = vm_end - vm_start;
		if (vm_start <= start && vm_end >= end){
				return  __hyplet_map_user_data(start,
						size, vma->vm_flags, hyp);
		}
	}

	return -EINVAL;
}
/*
 *  scan the process's vmas and map all possible pages
 */
int hyplet_map_all(struct hyplet_vm *hyp)
{
	struct vm_area_struct* vma;

	vma = current->mm->mmap;

	for (; vma ; vma = vma->vm_next) {
		long start = vma->vm_start;
		for ( ; start < vma->vm_end ; start += PAGE_SIZE){
			 __hyplet_map_user_data(start, PAGE_SIZE, vma->vm_flags, hyp);
		}
	}
	return 0;
}

int hyplet_check_mapped(struct hyplet_vm *hyp,struct hyplet_map_addr  *uaddr)
{
	if (hyplet_get_addr_segment(uaddr->addr ,hyp)) {
		hyplet_debug(" address %lx already mapped\n",uaddr->addr);
		return 1;
	}
	return 0;
}


void hyplet_flush_caches(struct hyplet_vm *tv)
{
        struct hyp_addr* tmp;
        unsigned long flags = 0;

        spin_lock_irqsave(&tv->lst_lock, flags);

        list_for_each_entry(tmp, &tv->hyp_addr_lst, lst) {
        	hyplet_call_hyp(hyplet_invld_tlb,  tmp->addr);
        	if (tmp->flags & VM_EXEC) {
        		hyplet_call_hyp(hyplet_flush_el2_icache,  tmp->addr);
        		continue;
        	}
        	hyplet_call_hyp(hyplet_flush_el2_dcache,  tmp->addr);
        }
        hyplet_call_hyp(hyplet_invld_all_tlb);
        spin_unlock_irqrestore(&tv->lst_lock, flags);
}

void hyplet_free_mem(struct hyplet_vm *tv)
{
        struct hyp_addr* tmp,*tmp2;
        unsigned long flags = 0;

        spin_lock_irqsave(&tv->lst_lock, flags);

        list_for_each_entry_safe(tmp, tmp2, &tv->hyp_addr_lst, lst) {

        	hyplet_debug("unmap %lx size=%d "
        			"pages=%d flags=%x\n",
        		tmp->addr,
				tmp->size,
				tmp->nr_pages, tmp->flags);

        	hyp_user_unmap( tmp->addr , PAGE_SIZE,  1 );
        	hyplet_call_hyp(hyplet_invld_tlb,  tmp->addr);

         	if (tmp->flags & VM_EXEC)
        			flush_icache_range(tmp->addr, tmp->addr + tmp->size);

         	if (tmp->flags & (VM_READ | VM_WRITE))
    			__flush_cache_user_range(tmp->addr, tmp->addr + tmp->size);

		  	list_del(&tmp->lst);
        	kfree(tmp);
         }
    //    hyplet_call_hyp(hyplet_invld_all_tlb);
        spin_unlock_irqrestore(&tv->lst_lock, flags);
}

