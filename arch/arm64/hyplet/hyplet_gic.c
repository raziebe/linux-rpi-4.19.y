#include <linux/module.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/linkage.h>

#include <linux/init.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <linux/vmalloc.h>
#include <asm/fixmap.h>
#include <asm/memory.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-common.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <asm/arch_gicv3.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/sched/signal.h>

#include <linux/delay.h>
#include <linux/hyplet.h>
#include <linux/hyplet_user.h>
#include "hypletS.h"

int hyplet_imp_timer(struct hyplet_vm *hyp)
{
	hyp->irq_to_trap = IRQ_TRAP_ALL;

	if (!(hyp->state & USER_CODE_MAPPED)){
		return -EINVAL;
	}

	hyp->tsk = current;
	hyplet_info("Implement timer\n");
	return 0;
}

int hyplet_trap_irq(struct hyplet_vm *tv,int irq)
{
	tv->tsk = current;
	if (!(tv->state & USER_CODE_MAPPED)){
		return -EINVAL;
	}

	tv->irq_to_trap = irq;
	hyplet_info("Trapping irq %d\n", irq);
	return 0;
}

int hyplet_untrap_irq(struct hyplet_vm *hyp, int irq)
{
	hyplet_reset(current);
	return 0;
}
//#define __GPIO__

#ifdef __GPIO__
#include <linux/gpio.h>
int gpio = 475; // the gpio we toggle
static int toggle = 0;
#endif

int hyplet_run(int irq)
{
	struct hyplet_vm *hyp;
	
	hyp = hyplet_get(raw_smp_processor_id());
	if (hyp->tsk == NULL)
		return 0; 

	if (irq == hyp->irq_to_trap
			|| hyp->irq_to_trap ==  IRQ_TRAP_ALL) {

		hyplet_call_hyp(hyplet_run_user);
		if (hyp->faulty_elr_el2){
			printk("hyplet isr abort elr_el2 0x%lx esr_el2=%lx\n",
					hyp->faulty_elr_el2 , hyp->faulty_esr_el2 );
			hyp->irq_to_trap = 0;
			hyp->faulty_elr_el2 = 0;
			hyp->faulty_esr_el2 = 0;
			force_sigsegv(SIGSEGV , hyp->tsk);
		}
#ifdef __GPIO__
	// it is expected that the gpio would be 
	// exported and configured from user space
	gpio_set_value(gpio, toggle);
	toggle = !toggle;
#endif
	}
	return 0; // TODO
}

