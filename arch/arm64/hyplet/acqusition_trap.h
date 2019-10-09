#ifndef __MALWARE_TRAP_H__
#define __MALWARE_TRAP_H__


void acqusion_init_procfs(void);
void acqusition_prep_mmio(char *addr);
void prepare_special_addresses(struct hyplet_vm *vm);

void dump_ipa(struct hyplet_vm *vm);

#if defined(RASPBERRY_PI3)
		#define EL2_FAULT_ADDRESS  0x3fd00000LL
	#else
		#define EL2_FAULT_ADDRESS 0x1a000000LL
#endif

#define MODULE_NAME "hyp_ops: "

static inline unsigned long el2_fault_address(void){
		return EL2_FAULT_ADDRESS;
}

#endif
