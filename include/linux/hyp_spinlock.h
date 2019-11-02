#ifndef __HYP_SPINLOCK_H__
#define __HYP_SPINLOCK_H__

typedef struct  {
    unsigned short lock;
} hyp_spinlock_t;

/*
 * Code taken from the Linux kernel
 */

static inline void hyp_spin_lock_init(hyp_spinlock_t *lock)
{
	lock->lock = 0;
}

static inline void hyp_spin_lock(hyp_spinlock_t *lock)
{
        unsigned int tmp;

        asm volatile(
        "       sevl\n"
        "1:     wfe\n"
        "2:     ldaxr   %w0, %1\n"
        "       cbnz    %w0, 1b\n"
        "       stxr    %w0, %w2, %1\n"
        "       cbnz    %w0, 2b\n"
        : "=&r" (tmp), "+Q" (lock->lock)
        : "r" (1)
        : "cc", "memory");
}


static inline void hyp_spin_unlock(hyp_spinlock_t *lock)
{
        asm volatile(
        "       stlr    %w1, %0\n"
        : "=Q" (lock->lock) : "r" (0) : "memory");
}


#endif
