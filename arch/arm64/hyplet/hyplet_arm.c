/*
 * Copyright (C) 2012 - TrulyProtect Jayvaskula University Findland
 * Author: Raz
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

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/arm-cci.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>

#include <linux/sched.h>
#include <asm/tlbflush.h>
#include <linux/slab.h>
#include <asm/virt.h>
#include <asm/sections.h>

#include <linux/hyplet.h>
#include "hyp_mmu.h"
#include "hypletS.h"

static DEFINE_PER_CPU(unsigned long, hyp_stack_page);

static inline void __cpu_init_hyp_mode(phys_addr_t boot_pgd_ptr,
                                       phys_addr_t pgd_ptr,
                                       unsigned long hyp_stack_ptr,
                                       unsigned long vector_ptr)
{
	hyplet_call_hyp((void *)boot_pgd_ptr, (void*)pgd_ptr, (void*)hyp_stack_ptr, vector_ptr);
}

unsigned long get_hyp_vector(void)
{
	return (unsigned long)__hyplet_vectors;
}


static void cpu_init_hyp_mode(void *discard)
{
	struct hyplet_vm *hyp;
	phys_addr_t pgd_ptr;
	phys_addr_t boot_pgd_ptr;
	unsigned long hyp_stack_ptr;
	unsigned long stack_page;
	unsigned long vector_ptr;

	/* Switch from the HYP stub to our own HYP init vector */
	__hyp_set_vectors(hyp_get_idmap_vector());

	pgd_ptr = hyp_mmu_get_httbr();
	stack_page = __this_cpu_read(hyp_stack_page);
	boot_pgd_ptr = hyp_mmu_get_boot_httbr();
	hyp_stack_ptr = stack_page + PAGE_SIZE;
	vector_ptr = get_hyp_vector();
	/*
	 * Switch to the hyplet vector
	 */
	__cpu_init_hyp_mode(boot_pgd_ptr, pgd_ptr, hyp_stack_ptr, vector_ptr);
	hyp = hyplet_get_vm();
	hyplet_map_to_el2(hyp);
	make_mair_el2(hyp);
	hyplet_call_hyp(hyplet_on, hyp);
}

/**
 * Inits Hyp-mode on all online CPUs
 */
static int init_hyp_mode(void)
{
	int cpu;
	int err = 0;

	/*
	 * Allocate Hyp PGD and setup Hyp identity mapping
	 */
	err = hyp_mmu_init();
	if (err)
		goto out_err;


	/*
	 * Allocate stack pages for Hypervisor-mode
	 */
	for_each_possible_cpu(cpu) {
		unsigned long stack_page;

		stack_page = __get_free_page(GFP_KERNEL);
		if (!stack_page) {
			err = -ENOMEM;
			goto out_err;
		}

		per_cpu(hyp_stack_page, cpu) = stack_page;
	}
	/*
	 * Map the Hyp-code called directly from the host
	 */
	err = create_hyp_mappings(__hyp_text_start, __hyp_text_end, PAGE_HYP_EXEC);
	if (err) {
		printk("Cannot map world-switch code\n");
		goto out_err;
	}

	err = create_hyp_mappings(__hyp_idmap_text_start, 
			 __hyp_idmap_text_end, PAGE_HYP_EXEC);
	if (err) {
            printk("Cannot map world-switch code\n");
            return -1;
	}

	err = create_hyp_mappings(__bss_start, __bss_stop, PAGE_HYP);
	if (err) {
		printk("Cannot map bss section\n");
		goto out_err;
	}

	/*
	 * Map the Hyp stack pages
	 */
	for_each_possible_cpu(cpu) {
		char *stack_page;

		stack_page = (char *)per_cpu(hyp_stack_page, cpu);
		err = create_hyp_mappings(stack_page, stack_page + PAGE_SIZE, PAGE_HYP);
		if (err) {
			printk("Cannot map hyp stack\n");
			goto out_err;
		}

	}
	return 0;

out_err:
	printk("error initializing Hyp mode: %d\n", err);
	return err;
}

#if 0 // older kernels
static int __attribute_const__  target_cpu(void)
{
	switch (read_cpuid_part()) {
	case ARM_CPU_PART_CORTEX_A7:
		return TP_ARM_TARGET_CORTEX_A7;
	case ARM_CPU_PART_CORTEX_A15:
		return TP_ARM_TARGET_CORTEX_A15;
	default:
		return -EINVAL;
	}
}

#else

#define ARM_TARGET_GENERIC_V8 1

static int __attribute_const__ target_cpu(void)
{
        unsigned long implementor = read_cpuid_implementor();
        unsigned long part_number = read_cpuid_part_number();

        switch (implementor) {
        	case ARM_CPU_IMP_ARM:
                switch (part_number) {
                case ARM_CPU_PART_AEM_V8:
                        return ARM_CPU_PART_AEM_V8;
                case ARM_CPU_PART_FOUNDATION:
                		return ARM_CPU_PART_FOUNDATION;
                case ARM_CPU_PART_CORTEX_A53:
                        return ARM_CPU_PART_CORTEX_A53;
                case ARM_CPU_PART_CORTEX_A57:
                        return ARM_CPU_PART_CORTEX_A57;
                }
                break;
            case ARM_CPU_IMP_APM:
                        switch (part_number) {
                        case APM_CPU_PART_POTENZA:
                                return APM_CPU_PART_POTENZA;
                        };
                        break;
         };
        /* Return a default generic target */
        return ARM_TARGET_GENERIC_V8;
}

#endif

static void check_target_cpu(void *ret)
{
	*(int *)ret = target_cpu();
}

/**
 * Initialize Hyp-mode and memory mappings on all CPUs.
 */
static int hyplet_arch_init(void)
{
	struct hyplet_vm *hyp, *this_hyp;
	int err;
	int ret, cpu;

	if (!is_hyp_mode_available()) {
		printk("HYP mode not available\n");
		return -ENODEV;
	}

	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu, check_target_cpu, &ret, 1);
		if (ret < 0) {
			printk("Error, CPU %d not supported!\n", cpu);
			return -ENODEV;
		}
	}

	printk("HYP mode is available rc-26\n");
	err = init_hyp_mode();
	if (err)
		return -1;


	this_hyp = hyplet_get_vm();
	INIT_LIST_HEAD(&this_hyp->hyp_addr_lst);
	INIT_LIST_HEAD(&this_hyp->callbacks_lst);
	spin_lock_init(&this_hyp->lst_lock);
	this_hyp->state = HYPLET_OFFLINE_ON;
	this_hyp->hcr_el2 =  HCR_RW;
#ifdef __HYPLET_VM__
	this_hyp->hcr_el2 |= HCR_VM;
#endif
	/* initialize VM if needed */
	hyplet_init_ipa();
	this_hyp->iomemaddr =  kmalloc(sizeof(struct IoMemAddr), GFP_KERNEL);
	memset(this_hyp->iomemaddr, 0x00, sizeof(struct IoMemAddr));
	err = create_hyp_mappings((unsigned char  *)this_hyp->iomemaddr,
			((unsigned char  *)this_hyp->iomemaddr) + sizeof(struct IoMemAddr), PAGE_HYP);
	if (err){
		hyplet_err("Failed to map iomemAddr\n");
		return -1;
	}

	for_each_possible_cpu(cpu) {

		if (raw_smp_processor_id() == cpu)
			continue;

		hyp = hyplet_get(cpu);
		INIT_LIST_HEAD(&hyp->hyp_addr_lst);
		INIT_LIST_HEAD(&hyp->callbacks_lst);
		spin_lock_init(&hyp->lst_lock);
		hyp->state = this_hyp->state;
		hyp->hcr_el2 =  this_hyp->hcr_el2;
		hyp->vtcr_el2 = this_hyp->vtcr_el2;
		hyp->vttbr_el2 = this_hyp->vttbr_el2;
		hyp->mair_el2 = this_hyp->mair_el2;
		hyp->iomemaddr  = this_hyp->iomemaddr;
	}

	on_each_cpu(cpu_init_hyp_mode, NULL,1);
	if (this_hyp->hcr_el2 & HCR_VM) {
		hyplet_info("Microvisor VM Initialized\n");
		} else{
		hyplet_info("Microvisor Initialized\n");
	}

	if (map_ipa_to_el2(this_hyp)){
		printk("Failed to map IPA\n");
		return -1;
	}

	for_each_possible_cpu(cpu) {
		if (raw_smp_processor_id() == cpu)
			continue;
		hyp = hyplet_get(cpu);
		hyp->ipa_desc_zero =  this_hyp->ipa_desc_zero;
		hyp->vttbr_el2_kern = this_hyp->vttbr_el2_kern;
		hyp->vttbr_el2 	   = this_hyp->vttbr_el2;
		hyp->hyp_memstart_addr = this_hyp->hyp_memstart_addr;
	}

	return 0;
}

module_init(hyplet_arch_init);
