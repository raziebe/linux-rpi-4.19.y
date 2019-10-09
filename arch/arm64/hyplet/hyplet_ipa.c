#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/hyplet.h>
#include <linux/delay.h>
#include "acqusition_trap.h"
#include "hyp_mmu.h"
#include "hypletS.h"


extern s64 memstart_addr;
phys_addr_t kaddr_to_phys(void *kaddr);

void map_stage3_to_hypervisor(unsigned long *addr)
{
	int err;
	unsigned long *vaddr;
	unsigned long phys_addr;
	struct page* page;
	extern pgd_t *hyp_pgd;
	unsigned long start;

	page = phys_to_page( (*addr) - PAGE_SIZE);
	vaddr = kmap(page);

	if (!is_vmalloc_addr(vaddr)) {
		if (!virt_addr_valid(vaddr)){
			kunmap(page);
			return;
		}
	}
	start = KERN_TO_HYP((unsigned long)vaddr);

	phys_addr = kaddr_to_phys(vaddr);

	err = __create_hyp_mappings(hyp_pgd, start,
				    		start + PAGE_SIZE,
							__phys_to_pfn(phys_addr),
							PAGE_HYP);
	if (err)
		printk("Failed to map IPA virt %p\n", vaddr);

	kunmap(page);
}

//
// alloc 512 * 4096  = 2MB
//
#define MEM_ATTR_SHIFT 2


void create_level_three(struct page *pg, unsigned long *addr)
{
	int i;
	long *l3_desc;

	l3_desc = (long *) kmap(pg);
	if (l3_desc == NULL) {
		printk("%s desc NULL\n", __func__);
		return;
	}
	memset(l3_desc, 0x00, PAGE_SIZE);
	for (i = 0; i < PAGE_SIZE / sizeof(long long); i++) {
		l3_desc[i] = (DESC_AF) |
						(0b11 << DESC_SHREABILITY_SHIFT) |
						/* The S2AP data access permissions, Non-secure EL1&0 translation regime  */
						(S2_PAGE_ACCESS_RW << DESC_S2AP_SHIFT) | (0b1111 << MEM_ATTR_SHIFT) |
						DESC_TABLE_BIT | DESC_VALID_BIT | (*addr);
		(*addr) += PAGE_SIZE;
		map_stage3_to_hypervisor(addr);
	}

	kunmap(pg);
}

// 1GB
void create_level_two(struct page *pg, long *addr)
{
	int i;
	long *l2_desc;
	struct page *pg_lvl_three;

	l2_desc = (long *) kmap(pg);
	if (l2_desc == NULL) {
		printk("%s desc NULL\n", __func__);
		return;
	}
	memset(l2_desc, 0x00, PAGE_SIZE);
	pg_lvl_three = alloc_pages(GFP_KERNEL | __GFP_ZERO, 9);
	if (pg_lvl_three == NULL) {
		printk("%s alloc page NULL\n", __func__);
		return;
	}

	for (i = 0; i < PAGE_SIZE / (sizeof(long)); i++) {
		// fill an entire 2MB of mappings
		create_level_three(pg_lvl_three + i, addr);
		// calc the entry of this table
		l2_desc[i] =
		    (page_to_phys(pg_lvl_three + i)) | DESC_TABLE_BIT |
		    DESC_VALID_BIT;
	}

	kunmap(pg);
}

void create_level_one(struct page *pg, long *addr)
{
	int i;
	long *l1_desc;
	struct page *pg_lvl_two;
	int lvl_two_nr_pages =16;

	l1_desc = (long *) kmap(pg);
	if (l1_desc == NULL) {
		printk("%s desc NULL\n", __func__);
		return;
	}
	memset(l1_desc,0x00, PAGE_SIZE);
	pg_lvl_two = alloc_pages(GFP_KERNEL | __GFP_ZERO, 4);
	if (pg_lvl_two == NULL) {
		printk("%s alloc page NULL\n", __func__);
		return;
	}

	for (i = 0; i < lvl_two_nr_pages ; i++) {
		get_page(pg_lvl_two + i);
		create_level_two(pg_lvl_two + i, addr);
		l1_desc[i] = (page_to_phys(pg_lvl_two + i)) | DESC_TABLE_BIT | DESC_VALID_BIT;

	}
	kunmap(pg);
}

void create_level_zero(struct hyplet_vm *vm, long* desc0, long *addr)
{
	struct page *pg_lvl_one;

	pg_lvl_one = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!pg_lvl_one) {
		printk("%s alloc page NULL\n", __func__);
		return;
	}

	get_page(pg_lvl_one);
	create_level_one(pg_lvl_one, addr);

	memset(desc0, 0x00, PAGE_SIZE);
	desc0[0] = (page_to_phys(pg_lvl_one)) | DESC_TABLE_BIT | DESC_VALID_BIT;
	vm->pg_lvl_one = (unsigned long)pg_lvl_one;

}

void hyplet_init_ipa(void)
{
	long addr = 0;
	long vmid = 012;
	struct page *pg_lvl0;
	int starting_level = 1;
	struct hyplet_vm *vm = hyplet_get_vm();

/*
 tosz = 25 --> 39bits 64GB
	0-11
2       12-20   :512 * 4096 = 2MB per entry
1	21-29	: 512 * 2MB = per page
0	30-35 : 2^5 entries	, each points to 32 pages in level 1
 	pa range = 1 --> 36 bits 64GB

*/
	pg_lvl0 = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (pg_lvl0 == NULL) {
		printk("%s alloc page NULL\n", __func__);
		return;
	}

	vm->ipa_desc_zero = (long *) kmap(pg_lvl0);
	if (vm->ipa_desc_zero == NULL) {
		printk("%s desc0 failed to map\n", __func__);
		return;
	}

	create_level_zero(vm, vm->ipa_desc_zero, &addr);

	if (starting_level == 0){
		vm->vttbr_el2 = page_to_phys(pg_lvl0) | (vmid << 48);
		vm->vttbr_el2_kern =  (unsigned long *)(page_to_virt(pg_lvl0));
	} else {
		vm->vttbr_el2 = page_to_phys((struct page *) vm->pg_lvl_one) | (vmid << 48);
		vm->vttbr_el2_kern =  (unsigned long *)(page_to_virt(vm->pg_lvl_one));
	}

	acqusion_init_procfs();
	make_vtcr_el2(vm);
	vm->hyp_memstart_addr = memstart_addr;
}


// D-2142
void make_vtcr_el2(struct hyplet_vm *vm)
{
	long vtcr_el2_t0sz;
	long vtcr_el2_sl0;
	long vtcr_el2_irgn0;
	long vtcr_el2_orgn0;
	long vtcr_el2_sh0;
	long vtcr_el2_tg0;
	long vtcr_el2_ps;

	vtcr_el2_t0sz = hyplet_get_tcr_el1() & 0b111111;
	vtcr_el2_sl0 = 0b01;	//IMPORTANT start at level 1.  D.2143 + D4.1746
	vtcr_el2_irgn0 = 0b1;
	vtcr_el2_orgn0 = 0b1;
	vtcr_el2_sh0 = 0b11;	// inner sharable
	vtcr_el2_tg0 = (hyplet_get_tcr_el1() & 0xc000) >> 14;
	vtcr_el2_ps = (hyplet_get_tcr_el1() & 0x700000000) >> 32;

	vm->vtcr_el2 = (vtcr_el2_t0sz) |
	    (vtcr_el2_sl0 << VTCR_EL2_SL0_BIT_SHIFT) |
	    (vtcr_el2_irgn0 << VTCR_EL2_IRGN0_BIT_SHIFT) |
	    (vtcr_el2_orgn0 << VTCR_EL2_ORGN0_BIT_SHIFT) |
	    (vtcr_el2_sh0 << VTCR_EL2_SH0_BIT_SHIFT) |
	    (vtcr_el2_tg0 << VTCR_EL2_TG0_BIT_SHIFT) |
	    (vtcr_el2_ps << VTCR_EL2_PS_BIT_SHIFT);

	printk("Using %d bits address space\n",get_el1_address_size());
}

/*
 * the page us using attr_ind 4
 */
void make_mair_el2(struct hyplet_vm *vm)
{
	unsigned long mair_el2;

	mair_el2 = hyplet_call_hyp(read_mair_el2);
	vm->mair_el2 = (mair_el2 & 0x000000FF00000000L ) | 0x000000FF00000000L; //
	//tvm->mair_el2 = 0xFFFFFFFFFFFFFFFFL;
 	hyplet_call_hyp(set_mair_el2, vm->mair_el2);
}




/*
 * walk on the IPA and map it to the hypervisor
 */
int map_ipa_to_el2(struct hyplet_vm *vm)
{
	int i,j,k, n;
	unsigned long *desc0 = vm->ipa_desc_zero;
	unsigned long temp;

	if ( create_hyp_mappings(desc0,
			(void *)((unsigned long)desc0 + PAGE_SIZE- 1), PAGE_HYP) ){

		printk("Failed to map desc0\n");
		return -1;
	}

	for ( i = 0 ; i < PAGE_SIZE/sizeof(long); i++){
		if (desc0[i]) {
			unsigned long *desc1;
			struct page *desc1_page;

			temp = desc0[i] & 0x000FFFFFFFFFFC00LL;
			desc1_page = phys_to_page(temp);
			desc1 = kmap(desc1_page);
			if ( create_hyp_mappings(desc1,
					(void *)((unsigned long)desc1 + PAGE_SIZE - 1), PAGE_HYP) ){
				printk("Failed to map desc1 to EL2\n");
				return -1;
			}

			for (j = 0 ; j < PAGE_SIZE/sizeof(long); j++){
				if (desc1[j]){
					unsigned long *desc2;
					struct page *desc2_page;

					temp = desc1[j] & 0x000FFFFFFFFFFC00LL;

					desc2_page = phys_to_page(temp);
					desc2 = kmap(desc2_page);
					create_hyp_mappings(desc2,
							(void *)((unsigned long)desc2 + PAGE_SIZE- 1), PAGE_HYP);


					for (k = 0 ; k < PAGE_SIZE/sizeof(long); k++){
						if (desc2[k]){
							struct page *desc3_page;
							unsigned long *desc3;

							temp = desc2[k] & 0x000FFFFFFFFFFC00LL;

							desc3_page = phys_to_page(temp);
							desc3 = kmap(desc3_page);
							create_hyp_mappings(desc3,
									(void *)((unsigned long)desc3 + PAGE_SIZE- 1), PAGE_HYP);

							for (n = 0 ; n < PAGE_SIZE/sizeof(long); n++){
								if (desc3[n]){
									temp = desc3[n] & 0x000FFFFFFFFFFC00LL;
								}
							}

							kunmap(desc3_page);
						}
					}

					kunmap(desc2_page);
				}
			}

			kunmap(desc1_page);
		}
	}
	return 0;
}
