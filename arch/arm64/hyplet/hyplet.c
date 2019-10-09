#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/sections.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include<linux/workqueue.h>

#include <linux/hyplet.h>
#include <linux/hyplet_user.h>
#include "hyp_mmu.h"
#include "hypletS.h"

int hyplet_map_user_vma(struct hyplet_vm *hyp,struct hyplet_ctrl *hypctl);
int hyplet_map_user(struct hyplet_vm *hyp,struct hyplet_ctrl *hypctl);

DEFINE_PER_CPU(struct hyplet_vm, HYPLETS);

struct hyplet_vm* hyplet_get(int cpu){
	return &per_cpu(HYPLETS, cpu);
}

struct hyplet_vm* hyplet_get_vm(void){
	return this_cpu_ptr(&HYPLETS);
}
/*
 * construct page table
*/
int hyplet_init(void)
{
	struct hyplet_vm *tv;
	int cpu = 0;

	tv = hyplet_get_vm();
	memset(tv, 0x00, sizeof(*tv));

	for_each_possible_cpu(cpu) {
		struct hyplet_vm *hyp = &per_cpu(HYPLETS, cpu);
		if (tv != hyp) {
			memcpy(hyp, tv, sizeof(*tv));
		}
		INIT_LIST_HEAD(&hyp->hyp_addr_lst);
		INIT_LIST_HEAD(&hyp->callbacks_lst);
		spin_lock_init(&hyp->lst_lock);

		hyp->state = HYPLET_OFFLINE_ON;
	}
	return 0;
}


void hyplet_map_to_el2(struct hyplet_vm *hyp)
{
	int err;

	err = create_hyp_mappings(hyp, hyp + 1, PAGE_HYP);
	if (err) {
		hyplet_err("Failed to map hyplet state");
		return;
	} else {
		hyplet_info("Mapped hyplet state");
	}
}

int __hyp_text is_hyp(void)
{
        u64 el;
        asm("mrs %0,CurrentEL" : "=r" (el));
        return el == CurrentEL_EL2;
}

int is_hyplet_on(void)
{
	struct hyplet_vm *hyp = hyplet_get_vm();
	return (hyp->irq_to_trap != 0);
}

void __close_hyplet(void *task, struct hyplet_vm *hyp)
{
	struct task_struct *tsk;
	tsk =  (struct task_struct *)task;

	if (hyp->tsk->mm != tsk->mm)
		return;

	if (!(hyp->state & HYPLET_OFFLINE_ON))
		hyplet_call_hyp(hyplet_mdcr_off);
	hyplet_call_hyp(hyplet_mdcr_off);
	hyp->tsk = NULL;
	smp_mb();
	while (hyp->state & HYPLET_OFFLINE_RUN) {
		msleep(1);
		hyplet_debug("Waiting for offlet to exit..\n");
	}

	hyp->irq_to_trap = 0;
	hyp->hyplet_id = 0;
	hyp->user_hyplet_code = 0;
	hyplet_free_mem(hyp);
	hyp->state  = HYPLET_OFFLINE_ON;
	smp_mb();
	hyp->elr_el2 = 0;
	hyp->hyplet_stack = 0;
	hyp->user_hyplet_code = 0;
	smp_mb();
	hyp->hyplet_id  = 0;
	hyplet_info("Close hyplet\n");
}

void close_hyplet(void *task)
{
	struct hyplet_vm *hyp = hyplet_get_vm();

	if (!hyp->tsk) {
		return;
	}
	__close_hyplet(task, hyp);
}

/*
 * call on each process shutdown
*/
void hyplet_reset(struct task_struct *tsk)
{
	int i;
	struct hyplet_vm *hyp;

	on_each_cpu(close_hyplet, tsk, 1);

	for (i = 0 ; i < num_possible_cpus(); i++){
		hyp = hyplet_get(i);

		if (hyp->state  & HYPLET_OFFLINE_RUN) {
				printk("hyplet offlet discoverred on cpu %d %p\n",
					i, hyp->tsk);
			__close_hyplet(tsk, hyp);
		}
	}
}

static void signal_any(struct hyplet_vm *hyp)
{
	unsigned long flags;
    struct hyp_wait *tmp;

    spin_lock_irqsave(&hyp->lst_lock, flags);

    list_for_each_entry(tmp, &hyp->callbacks_lst, next) {
		tmp->offlet_action(hyp, tmp);
	}
    spin_unlock_irqrestore(&hyp->lst_lock, flags);
}

static void offlet_wake(struct hyplet_vm *hyp,struct hyp_wait* hypevent)
{
	wake_up_interruptible(&hypevent->wait_queue);
}

/*
 * called from a kernel driver context
*/
void offlet_register(struct hyp_wait* hypeve,int cpu)
{
	unsigned long flags;
	struct hyplet_vm *hyp;

	hyp = hyplet_get(cpu);
	spin_lock_irqsave(&hyp->lst_lock, flags);
	list_add(&hypeve->next, &hyp->callbacks_lst);
	spin_unlock_irqrestore(&hyp->lst_lock, flags);
}
EXPORT_SYMBOL_GPL(offlet_register);

/*
 * called from a kernel driver context
*/
void offlet_unregister(struct hyp_wait* hypeve,int cpu)
{
	unsigned long flags;
	struct hyplet_vm *hyp;

	hyp = hyplet_get(cpu);
	
	spin_lock_irqsave(&hyp->lst_lock, flags);
	list_del(&hypeve->next);
	spin_unlock_irqrestore(&hyp->lst_lock, flags);
}
EXPORT_SYMBOL_GPL(offlet_unregister);

static void wait_for_hyplet(struct hyplet_vm *hyp,int ms)
{
	unsigned long flags;
	struct hyp_wait hypevent;

	hypevent.offlet_action = offlet_wake;

	init_waitqueue_head(&hypevent.wait_queue);

	spin_lock_irqsave(&hyp->lst_lock, flags);
	list_add(&hypevent.next ,&hyp->callbacks_lst);
	spin_unlock_irqrestore(&hyp->lst_lock, flags);

	wait_event_timeout(hypevent.wait_queue, 1, ms);

	spin_lock_irqsave(&hyp->lst_lock, flags);
	list_del(&hypevent.next);
	spin_unlock_irqrestore(&hyp->lst_lock, flags);
}

void set_current(void *tsk)
{
        asm ("msr sp_el0, %0" : "=r" (tsk));
}

void hyplet_offlet(unsigned int cpu)
{
	struct hyplet_vm *hyp;

	hyp = hyplet_get_vm();
	printk("offlet : Enter %d %p\n",cpu, hyp->tsk);

	while (hyp->state & HYPLET_OFFLINE_ON) {
		struct task_struct *tsk = 0x00;
		/*
		 * Wait for an assignment.
		 */
		while (hyp->tsk == NULL
				|| hyp->user_hyplet_code == 0x00){
			cpu_relax();
		}
		tsk = hyp->tsk;
		set_current(tsk);
		hyp->state |= HYPLET_OFFLINE_RUN;

		printk("hyplet offlet: Start run\n");
		while (hyp->tsk != NULL &&
						(hyp->user_hyplet_code != 0x00)) {
			hyplet_call_hyp(hyplet_run_user);
			signal_any(hyp);
		}
		set_current(tsk);
		hyplet_flush_caches(hyp);
		hyp->state &= ~(HYPLET_OFFLINE_RUN);
		smp_mb();
		printk("hyplet offlet : Ended\6n");
	}
	printk("offlet : Exit %d\n",cpu);
}

int hyplet_set_rpc(struct hyplet_ctrl* hplt,struct hyplet_vm *hyp)
{
	/*
	 * check that the function exists
	*/
	if ( hyp->user_hyplet_code !=
			hplt->rpc_set_func.func_addr ){
		hyplet_err("User hyplet is incorrect\n");
		return -EINVAL;
	}

	hyp->hyplet_id = hplt->rpc_set_func.func_id;
	hyp->tsk = current;
	hyplet_call_hyp(hyplet_mdcr_on);
	return 0;
}

int offlet_assign(int cpu,struct hyplet_ctrl* target_hplt,struct hyplet_vm *src_hyp)
{
	struct hyplet_vm *hyp = hyplet_get(cpu);

	if (hyp == NULL){
		hyplet_err("Failed to assign hyplet in %d\n",cpu);
		return -1;
	}
	hyp->state  = src_hyp->state;
	smp_mb();
	hyp->tsk = current;
	hyp->user_hyplet_code = target_hplt->addr.addr;
	hyp->hyplet_stack = src_hyp->hyplet_stack;
	smp_mb();
	printk("offlet: hyplet assigned to cpu %d\n",cpu);

	return 0;
}

int hyplet_ctl(unsigned long arg)
{
	struct hyplet_vm *hyp = hyplet_get_vm();
	struct hyplet_ctrl hplt;
	int rc = -1;

	if (hyp->tsk && hyp->tsk->mm != current->mm) {
		hyplet_err(" hyplet busy\n");
		return -EBUSY;
	}

	if (copy_from_user(&hplt, (void *) arg, sizeof(hplt)) ){
		hyplet_err(" failed to copy from user");
		return -1;
	}

	if (hplt.cpu >= 0){
		hyp = hyplet_get(hplt.cpu);
	}

	switch (hplt.cmd)
	{
		case HYPLET_MAP_ALL:
				return hyplet_map_all(hyp);

		case HYPLET_MAP:
				return hyplet_map_user(hyp, &hplt);

		case HYPLET_MAP_VMA:
				return hyplet_map_user_vma(hyp, &hplt);

		case HYPLET_MAP_STACK: // If the user won't map the stack we use the current sp_el0
				rc = hyplet_check_mapped(hyp, &hplt.addr);
				if ( rc == 0){
					hyplet_info("stack 0x%lx failed, not mapped\n",hplt.addr.addr);
					return -EINVAL;
				}
				hyp->hyplet_stack =
					(long)(hplt.addr.addr) +
						hplt.addr.size - PAGE_SIZE;
				break;

		case HYPLET_SET_CALLBACK:
				rc = hyplet_check_mapped(hyp, &hplt.addr);
				if (rc == 0) {
					hyplet_info("Warning: Hyplet was not mapped\n");
				}
				hyp->user_hyplet_code = hplt.addr.addr;
				break;

		case OFFLET_SET_CALLBACK: // must be called in the processor in which the stack was set.
				rc = hyplet_check_mapped(hyp, &hplt.addr);
				if (rc == 0) {
					hyplet_info("Warning: Hyplet was not mapped\n");
				}
				return offlet_assign(hplt.cpu, &hplt, hyp);

		case HYPLET_TRAP_IRQ:
				return hyplet_trap_irq(hyp,hplt.irq);

		case HYPLET_UNTRAP_IRQ:
				return hyplet_untrap_irq(hyp, hplt.irq);

		case HYPLET_IMP_TIMER:
				return hyplet_imp_timer(hyp);

		case HYPLET_SET_RPC:
				return hyplet_set_rpc(&hplt, hyp);

		case HYPLET_WAIT:
				 wait_for_hyplet(hyp, hplt.timeout_ms);
				 break;

		case HYPLET_REGISTER_PRINT:
				hyp->el2_log = hplt.addr.addr;
				rc = 0;
				break;
		case HYPLET_MDCR_ON:
				hyplet_call_hyp(hyplet_mdcr_on);
				rc = 0;
				break;

		case HYPLET_MDCR_OFF:
				hyplet_call_hyp(hyplet_mdcr_off);
				rc = 0;
				break;
	}
	return rc;
}
