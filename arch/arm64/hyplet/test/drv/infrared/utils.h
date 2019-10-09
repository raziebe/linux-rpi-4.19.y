#ifndef __HYPLET_UTILS_H__
#define __HYPLET_UTILS_H__

typedef unsigned long long u64;
typedef signed long long s64;
static const int arm_arch_timer_reread = 1;

/* QorIQ errata workarounds */
#define ARCH_TIMER_REREAD(reg) ({ \
	u64 _val_old, _val_new; \
	int _timeout = 200; \
	do { \
		asm volatile("mrs %0, " reg ";" \
			     "mrs %1, " reg \
			     : "=r" (_val_old), "=r" (_val_new)); \
		_timeout--; \
	} while (_val_old != _val_new && _timeout); \
	_val_old; \
})

#define ARCH_TIMER_READ(reg) ({ \
	u64 _val; \
	if (arm_arch_timer_reread) \
		_val = ARCH_TIMER_REREAD(reg); \
	else \
		asm volatile("mrs %0, " reg : "=r" (_val)); \
	_val; \
})

static inline long cycles(void) {
	long cval;
	cval = ARCH_TIMER_READ("cntvct_el0"); 		
	return cval;
}

static inline long cntvoff_el2(void)
{
	long val;
	asm ("mrs   %0, cntvoff_el2" : "=r" (val) );
	return val;
}

static inline u64 cycles_ns(void)
{
	u64 t1;
	u64 t = cycles();

	t1 = t * 52083;
	do_div(t1, 1000);
	return t1;
}


#endif

