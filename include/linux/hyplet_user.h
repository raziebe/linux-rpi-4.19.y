#ifndef __HYPLET_USER_H__
#define __HYPLET_USER_H__

typedef enum {
	   HYPLET_SET_CALLBACK = 1,
	   HYPLET_MAP_STACK = 2,
	   HYPLET_MAP_ALL = 3,
	   HYPLET_TRAP_IRQ = 4,
	   HYPLET_UNTRAP_IRQ = 5,
	   HYPLET_REGISTER_BH = 6, // register the task to wake up
	   HYPLET_SET_RPC  = 7,
	   HYPLET_IMP_TIMER = 8,
	   OFFLET_SET_CALLBACK = 9,
	   HYPLET_WAIT = 10,
	   HYPLET_EXECUTE = 11, // would execute the mapped hyplet 
	   HYPLET_REGISTER_PRINT = 12,
	   HYPLET_MAP = 13,
	   HYPLET_MAP_VMA = 14,
	   HYPLET_MDCR_ON = 15,
	   HYPLET_MDCR_OFF = 16
} hyplet_ops;


struct hyplet_map_addr {
	unsigned long addr  __attribute__ ((packed));
	int size   __attribute__ ((packed));
};

struct hyplet_irq_action {
	int action;
	int irq __attribute__ ((packed));
};

struct hyplet_rpc_set {
	long func_addr;
	int func_id __attribute__ ((packed));
};

struct hyplet_ctrl {
	int cmd  __attribute__ ((packed));
	int irq  __attribute__ ((packed));
	int cpu  __attribute__ ((packed));
	int timeout_ms  __attribute__ ((packed));

	struct hyplet_map_addr 	addr;
	struct hyplet_rpc_set 	rpc_set_func; 
};

#endif

