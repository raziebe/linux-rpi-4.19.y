#ifndef  __HYPLETS_H__
#define __HYPLETS_H__

long 		hyplet_call_hyp(void *hyper_func, ...);
void 			hyplet_flush_el2_dcache(unsigned long);
void 			hyplet_flush_el2_icache(unsigned long);
void 			hyplet_mdcr_on(void);
void 			hyplet_mdcr_off(void);
int  			hyplet_run_user(void);
void 			hyplet_on(void *);
unsigned long   hyplet_smp_rpc(long val);

void 		hyplet_invld_tlb(unsigned long va);
void		hyplet_invld_all_tlb(void);
void		hyplet_flush_el2_icache(unsigned long va);
void		hyplet_flush_el2_dcache(unsigned long va);
unsigned long   hyplet_clear_cache(pte_t* addr,long size);
void 		hyplet_set_vectors(unsigned long vbar_el2);
unsigned long 	hyplet_get_vectors(void);
unsigned long    read_mair_el2(void);
void 		 set_mair_el2(unsigned long);
unsigned long __hyp_text hyplet_handle_abrt(struct hyplet_vm *vm, unsigned long paddr);
#endif
