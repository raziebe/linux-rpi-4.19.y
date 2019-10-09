#ifndef __HYPLET_H_
#define __HYPLET_H_

// page 1775
#define DESC_TABLE_BIT 			( UL(1) << 1 )
#define DESC_VALID_BIT 			( UL(1) << 0 )
#define DESC_XN	       			( UL(1) << 54 )
#define DESC_PXN	      		( UL(1) << 53 )
#define DESC_CONTG_BIT		 	( UL(1) << 52 )
#define DESC_AF	        		( UL(1) << 10 )
#define DESC_SHREABILITY_SHIFT		(8)
#define DESC_S2AP_SHIFT			(6)
#define DESC_MEMATTR_SHIFT		(2)

#define VTCR_EL2_T0SZ_BIT_SHIFT 	0
#define VTCR_EL2_SL0_BIT_SHIFT 		6
#define VTCR_EL2_IRGN0_BIT_SHIFT 	8
#define VTCR_EL2_ORGN0_BIT_SHIFT 	10
#define VTCR_EL2_SH0_BIT_SHIFT 		12
#define VTCR_EL2_TG0_BIT_SHIFT 		14
#define VTCR_EL2_PS_BIT_SHIFT 		16

#define SCTLR_EL2_EE_BIT_SHIFT		25
#define SCTLR_EL2_WXN_BIT_SHIFT		19
#define SCTLR_EL2_I_BIT_SHIFT		12
#define SCTLR_EL2_SA_BIT_SHIFT		3
#define SCTLR_EL2_C_BIT_SHIFT		2
#define SCTLR_EL2_A_BIT_SHIFT		1	
#define SCTLR_EL2_M_BIT_SHIFT		0

/* Hyp Configuration Register (HCR) bits */
#define HCR_ID		(UL(1) << 33)
#define HCR_CD		(UL(1) << 32)
#define HCR_RW_SHIFT	31
#define HCR_RW		(UL(1) << HCR_RW_SHIFT) //
#define HCR_TRVM	(UL(1) << 30)
#define HCR_HCD		(UL(1) << 29)
#define HCR_TDZ		(UL(1) << 28)
#define HCR_TGE		(UL(1) << 27)
#define HCR_TVM		(UL(1) << 26) // Trap Virtual Memory controls.
#define HCR_TTLB	(UL(1) << 25)
#define HCR_TPU		(UL(1) << 24)
#define HCR_TPC		(UL(1) << 23)
#define HCR_TSW		(UL(1) << 22)  // Trap data or unified cache maintenance
#define HCR_TAC		(UL(1) << 21)  // Trap Auxiliary Control Register
#define HCR_TIDCP	(UL(1) << 20)  // Trap IMPLEMENTATION DEFINED functionality
#define HCR_TSC		(UL(1) << 19)  // Trap SMC
#define HCR_TID3	(UL(1) << 18)
#define HCR_TID2	(UL(1) << 17)
#define HCR_TID1	(UL(1) << 16)
#define HCR_TID0	(UL(1) << 15)
#define HCR_TWE		(UL(1) << 14) // traps Non-secure EL0 and EL1 execution of WFE instructions to
#define HCR_TWI		(UL(1) << 13) // traps Non-secure EL0 and EL1 execution of WFI instructions to EL2,
#define HCR_DC		(UL(1) << 12)
#define HCR_BSU		(3 << 10)
#define HCR_BSU_IS	(UL(1) << 10) // Barrier Shareability upgrade
#define HCR_FB		(UL(1) << 9)  // Force broadcast.
#define HCR_VA		(UL(1) << 8)
#define HCR_VI		(UL(1) << 7)
#define HCR_VF		(UL(1) << 6)
#define HCR_AMO		(UL(1) << 5) //Physical SError Interrupt routing.
#define HCR_IMO		(UL(1) << 4)
#define HCR_FMO		(UL(1) << 3)
#define HCR_PTW		(UL(1) << 2)
#define HCR_SWIO	(UL(1) << 1) // Set/Way Invalidation Override
#define HCR_VM		(UL(1) << 0)

#define HYP_PAGE_OFFSET_SHIFT	VA_BITS
#define HYP_PAGE_OFFSET_MASK	((UL(1) << HYP_PAGE_OFFSET_SHIFT) - 1)
#define HYP_PAGE_OFFSET		(PAGE_OFFSET & HYP_PAGE_OFFSET_MASK)
#define KERN_TO_HYP(kva)	((unsigned long)kva - PAGE_OFFSET + HYP_PAGE_OFFSET)
#define HYP_TO_KERN(hpa)	((unsigned long)hpa + PAGE_OFFSET - HYP_PAGE_OFFSET)
#define USER_TO_HYP(uva)	(uva)
#define HYPLET_HCR_FLAGS 	(HCR_RW)

#define HYP_PAGE_OFFSET_HIGH_MASK	((UL(1) << VA_BITS) - 1)
#define HYP_PAGE_OFFSET_LOW_MASK	((UL(1) << (VA_BITS - 1)) - 1)

#define ESR_ELx_EC_SVC_64 0b10101
#define ESR_ELx_EC_SVC_32 0b10001

#define S2_PAGE_ACCESS_NONE	0b00
#define S2_PAGE_ACCESS_R	0b01
#define S2_PAGE_ACCESS_W	0b10
#define S2_PAGE_ACCESS_RW	0b11

#define __hyp_text __section(.hyp.text) notrace


#define __int8  char
typedef unsigned __int8 UCHAR;


#define	IRQ_TRAP_ALL			(UL(0xFFFF) )
#define USER_CODE_MAPPED		(UL(1) << 0)
#define HYPLET_OFFLINE_ON		(UL(1) << 1)
#define HYPLET_OFFLINE_RUN		(UL(1) << 2)
#define FAULT_MMIO_TO_EL2		0b10
#define FAULT_MAPPED_TO_EL2		0b01
#define FAULT_MAX_HANDLERS 10

struct hyp_addr {
	struct list_head lst;
	unsigned long addr;
	int size;
	int flags;
	int nr_pages;

};

struct hyplet_vm;


struct stage2_fault_addr {
	unsigned long real_phys_addr;
	unsigned long *stg2_desc_pg;
	void *fake_vaddr;
	unsigned long fake_phys_addr;
	int stg2_desc_idx;
	int flags;
};

struct virt_dev_access {
	unsigned long faulted_phys_addr;
	unsigned long last_current;
	unsigned long count;
	struct stage2_fault_addr faddr;
};

struct IoMemAddr {
	unsigned long iomemaddr[1000];
	unsigned long  ioaddressesNR;
};

struct hyplet_vm {
	unsigned int irq_to_trap __attribute__ ((packed));
	int	hyplet_id __attribute__ ((packed));//  the hyplet of this core
	unsigned long sp_el0;
	unsigned long user_arg1;
	unsigned long user_arg2;
	unsigned long user_arg3;
	unsigned long user_arg4;
	unsigned long elr_el2;
	unsigned long el2_log;

	unsigned long hyplet_stack;	// this core hyplet stack
	unsigned long user_hyplet_code;	// this core hyplet codes
	
	struct task_struct *tsk;
 	struct list_head callbacks_lst;
 	spinlock_t lst_lock;
 	unsigned long* ipa_desc_zero;
 	unsigned long pg_lvl_one;
 	unsigned long *vttbr_el2_kern;
 	struct list_head hyp_addr_lst;
 	unsigned long state __attribute__ ((packed));
	unsigned long faulty_elr_el2 __attribute__ ((packed));
	unsigned long faulty_esr_el2 __attribute__ ((packed));
	unsigned long vtcr_el2;
	unsigned long vttbr_el2;
	unsigned long hcr_el2;
	unsigned long mair_el2;
	s64 hyp_memstart_addr;	/* memstart_addr is use deduct the physical address */
	int ipa_pages;
	int ipa_pages_processed;
	struct IoMemAddr* iomemaddr;
} __attribute__ ((aligned (8)));

struct hyp_wait{
	wait_queue_head_t wait_queue;
	void (*offlet_action)(struct hyplet_vm *,struct hyp_wait *);
	struct list_head next;
};

extern char __hyplet_vectors[];

int  		hyplet_init(void);
void 		hyplet_smp_run_hyp(void);

void 		hyplet_setup(void);


int  		hyplet_map_user_data(int ops ,  void *action);
struct hyplet_ctrl;
int 		hyplet_map_user(struct hyplet_vm *hyp,struct hyplet_ctrl *hypctl);
int  		hyplet_trap_irq(struct hyplet_vm *, int irq);
int  		hyplet_untrap_irq(struct hyplet_vm *,int irq);
int  		hyplet_start(void);


void 		hyplet_free_mem(struct hyplet_vm *tv);
void 		hyplet_reset(struct task_struct *tsk);
void 		hyplet_user_unmap(unsigned long umem);

int  		hyplet_run(int irq);
int  		hyplet_trapped_irq(struct hyplet_vm *);
int			hyplet_dump_irqs(void);
int 		hyplet_hwirq_to_irq(int);
void 		hyplet_stop(void *info);
struct 		hyplet_vm* hyplet_get(int cpu);


unsigned long 	kvm_uaddr_to_pfn(unsigned long uaddr);
void 		hyplet_set_cxt(long addr);
int 		hyplet_imp_timer(struct hyplet_vm *);
struct hyplet_map_addr;
int 		hyplet_check_mapped(struct hyplet_vm *,struct hyplet_map_addr* action);
void 		hyplet_offlet(unsigned int cpu);
void 		hyplet_map_to_el2(struct hyplet_vm *hyp);
void 		hyplet_init_ipa(void);
struct hyplet_vm* hyplet_get_vm(void);
unsigned 	long hyplet_get_tcr_el1(void);
void 			make_vtcr_el2(struct hyplet_vm *tvm);
unsigned long __hyp_text get_hyplet_addr(int hyplet_id,struct hyplet_vm * hyp);
void 	make_mair_el2(struct hyplet_vm *vm);
int 	map_ipa_to_el2(struct hyplet_vm *vm);
void 	__hyp_text   walk_ipa_el2(struct hyplet_vm *vm,int s2_page_access);
unsigned long*  ipa_find_page_desc(struct hyplet_vm *vm,unsigned long phy_addr);
int is_hyp(void);
unsigned long __hyp_phys_to_virt(unsigned long addr,struct hyplet_vm *vm);
void test_ipa_settings(void);
void hyplet_invld_ipa_tlb(unsigned long phy_addr);

#define hyplet_info(fmt, ...) \
		pr_info("hyplet [%i]: " fmt, raw_smp_processor_id(), ## __VA_ARGS__)

#define hyplet_err(fmt, ...) \
		pr_err("hyplet [%i]: " fmt, raw_smp_processor_id(), ## __VA_ARGS__)

#define hyplet_debug(fmt, ...) \
		pr_debug("hyplet [%i]: " fmt, raw_smp_processor_id(), ## __VA_ARGS__)

#ifdef __HYPLET__

static inline void	__hyplet_run(int irq){
	if (irq)
		hyplet_run(irq);
}

void 	hyplet_reset(struct task_struct *tsk);
int  	hyplet_ctl(unsigned long arg);

#else

#define	__hyplet_run(a)
#define hyplet_reset(a)

static inline int  	hyplet_ctl(unsigned long arg){
	return -ENOSYS;
}

#endif

#endif
