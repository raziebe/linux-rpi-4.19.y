#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/memory.h>
#include <asm/asm-offsets.h>
#include "hyp_mmu.h"
#include "tp_hyp_flags.h"

#define HYP_PAGE_OFFSET_SHIFT   VA_BITS
#define HYP_PAGE_OFFSET_MASK    ((UL(1) << HYP_PAGE_OFFSET_SHIFT) - 1)
#define HYP_PAGE_OFFSET         (PAGE_OFFSET & HYP_PAGE_OFFSET_MASK)
#define KERN_TO_HYP(kva)        ((unsigned long)kva - PAGE_OFFSET + HYP_PAGE_OFFSET)

.text
.pushsection	.hyp.text, "ax"
.align	PAGE_SHIFT

.macro invalid_vector   label,num
.align 7
\label:
	mov	x6, \num
        b __truly_panic
ENDPROC(\label)
.endm

.macro	push, xreg1, xreg2
	stp	\xreg1, \xreg2, [sp, #-16]!
.endm

.macro	pop, xreg1, xreg2
	ldp	\xreg1, \xreg2, [sp], #16
.endm


.macro save_registers
	push x29, x30
	push x27, x28
	push x25, x26
	push x23, x24
	push x21, x22
	push x19, x20
	push x17, x18
	push x15, x16
	push x13, x14
	push x11, x12
	push x9,  x10
	push x7,  x8
	push x5,  x6
	push x3,  x4
	push x1,  x2
	push x0,  xzr
.endm


.macro restore_registers
	pop  x0,  xzr
	pop  x1,  x2
	pop  x3,  x4
	pop  x5,  x6
	pop  x7,  x8
	pop  x9,  x10
	pop  x11, x12
	pop  x13, x14
	pop  x15, x16
	pop  x17, x18
	pop  x19, x20
	pop  x21, x22
	pop  x23, x24
	pop  x25, x26
	pop	 x27, x28
	pop  x29, x30
.endm


EL1_sync: // EL1/EL0 --> EL2
	push	x0, x1
	push	x2, x3

	mrs	x1, esr_el2
	lsr	x2, x1, #ESR_ELx_EC_SHIFT	// Syndrom shift 26 bits

	cmp	x2, #ESR_ELx_EC_HVC64		// If not 10110 then we have a trap
	b.ne	el1_trap

	/* Here, we're pretty sure the host called HVC. */
	pop	x2, x3
	pop	x0, x1

	/* Check for __hyp_get_vectors */
	cbnz	x0, 1f
	mrs	x0, vbar_el2
	b	2f

1:	push	lr, xzr	
	/*
	 * Compute the function address in EL2, and shuffle the parameters.
	 */
	kern_hyp_va	x0
	mov	lr, x0		// function address
	mov	x0, x1		// the context
	mov	x1, x2
	mov	x2, x3
	blr	lr

	pop	lr, xzr
	
2:	eret

el1_trap:
	/*
	 * x1: ESR
	 * x2: ESR_EC
	 */
	cmp x2, #0x18				// MRC,MRS
	b.eq	4f
	cmp x2, #0x3c					// debug brk command
	b.eq	BRK_EXCEPTION

	cmp	x2, #ESR_ELx_EC_DABT_LOW	// ESR_ELx_EC_DABT_LOW = 0x24 = 36d = data abort
	mov	x0, #ESR_ELx_EC_IABT_LOW	// ESR_ELx_EC_IABT_LOW = 0x20 = 32 instruction abort 
	ccmp	x2, x0, #4, ne //  (x2 and x0 ) & 4
	b.ne	4f		// Not an abort we care about

4:
	pop	x2, x3
	pop	x0, x1
	
	eret
 

BRK_EXCEPTION:

	mrs	x0,tpidr_el2
// save user space version of sp_el0
	mrs	x1, sp_el0
	str x1,[x0, #TP_SP_EL0_USR]
	ldr x3,[x0, #TP_PGD]
	cmp x3, #0
	b.ne SAVE_LR
	mrs x3,elr_el2
	add x3,x3,#4
	msr elr_el2,x3

	pop	x2, x3
	pop	x0, x1
	eret

SAVE_LR:
	//
	// The exception code take place many times,
	// We must make sure we fill the caller address only once
    //
	ldr x3,[x0, #TP_FIRST_LR]
	cmp x3,#0
	b.ne NOT_FIRST
	str x30,[x0, #TP_FIRST_LR]	// save the caller return address

NOT_FIRST:

	mrs x2,elr_el2
	str x2,[x0, #TP_ELR_EL2]

	save_registers

	ldr x1,=truly_decrypt
	kern_hyp_va x1

	blr x1
	bl pad_invalidate

// save the thread context for the case of an mmu abort
	mrs x0, tpidr_el2
	mrs x1, tpidr_el0
	str x1, [x0,#TP_TPIDR_EL0]

	restore_registers
	pop	x2, x3
	pop	x0, x1

	msr cntvoff_el2,x1  //<-- save x1

//	force stack to el0
	mrs x1, spsel
	and w1,w1,#0xFFFFFFFE
	msr spsel,x1

	mrs x30, elr_el2	   // go back to the next command after the faulted one
	mrs x1,cntvoff_el2 // <-- restore x1

	msr cntvoff_el2,xzr

	blr x30
	// Execute decrypted in EL2  and then return to next command because of the blr call.

	// User called ret.
	// Return from the decrypted routine and then return to the
	// reconstruced x30 as elr_el2

	/*
	*	force stack back to elX as we cannot use the program's stack.
	*/
	msr cntvoff_el2 ,x1   //<-- save x1
	mrs x1, spsel
	orr w1,w1,#0x1
	msr spsel,x1

/*
 * force eret to EL0
*/

	mrs  x1,spsr_el2
	and w1,w1,#0xFFFFFFF0
	msr spsr_el2,x1

	mrs x1,cntvoff_el2 // <-- restore x1
	msr cntvoff_el2,xzr

	// pad the code
	save_registers
	mrs	x0,tpidr_el2
	str xzr,[x0, #TP_SAVE_CMD]
	ldr x1,=truly_pad
	kern_hyp_va x1
	blr x1
	bl pad_invalidate

	mrs	x0,tpidr_el2
	// reconstrcted caller
	ldr x30,[x0, #TP_FIRST_LR]
	msr  elr_el2,x30 // we eret, so we must fix the pc in EL0
	str xzr,[x0, #TP_FIRST_LR]

	restore_registers

	eret
ENDPROC(el1_trap)

ENTRY(pad_invalidate)

        mrs x1,tpidr_el2
        ldr x0,[x1, #TP_ENC]

		kern_hyp_va x0
		mov x2, xzr

        ldr w2,[x0, #TP_ENC_SIZE] // size
        ldr x3,[x0, #TP_PAD_DATA] // pad_data

        mov x0, x3        // start
      	add x1, x0,x2 // end

1:
        ic ivau, x0
        add x0,x0,#32
        cmp x0,x1
        ble 1b

        ret
ENDPROC(pad_invalidate)

__truly_panic:
	adr	x0, __hyp_panic_str
	adr	x1, 2f // adr generates a register-relative address in the destination register
	ldp	x2, x3, [x1] // Load to Pair of Registers from two dwords starting from memory at [x1] 
	sub	x0, x0, x2
	add	x0, x0, x3
	mrs	x1, spsr_el2
	mrs	x2, elr_el2
	mrs	x3, esr_el2
	mrs	x4, far_el2
	mrs	x5, hpfar_el2
	mov	x7, sp

	mov	lr, #(PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT |\
		      PSR_MODE_EL1h)
	msr	spsr_el2, lr
	ldr	lr, =panic
	msr	elr_el2, lr
	eret

	.align	3
2:	.quad	HYP_PAGE_OFFSET
	.quad	PAGE_OFFSET
ENDPROC(__truly_panic)
__hyp_panic_str:
	.ascii	"Truly panic:\nCode:%08x elr_el2:%016x ESR_EL2:%08x\nFAR_EL2:%016x" \
	" HPFAR:%016x ENTRY:%p\nSP:%p\n\0"

ENTRY(truly_get_tcr_el1)
       mrs     x0, tcr_el1
       ret
ENDPROC(truly_get_tcr_el1)

/* This procedure calls the default hypervisor vector and
 * and sets truly vector. This is because when the cpu drops
 * Linux calls smc and vbar_el2 reseets.
*/
ENTRY(truly_get_vectors)
	mov	x0, xzr
ENTRY(truly_set_vectors)
	hvc #0
	ret
ENDPROC(truly_set_vectors)

ENTRY(read_mair_el2)
	mrs x0, mair_el2
        ret
ENDPROC(read_mair_el2)

ENTRY(set_mair_el2)
	msr mair_el2,x0
    ret
ENDPROC(set_mair_el2)

ENTRY(truly_get_sp_el2)
	mov x0,sp
	ret
ENDPROC(truly_get_sp_el2)


ENTRY(read_ttbr0_el2)
	mrs x0,ttbr0_el2
   	ret
ENDPROC(read_ttbr0_el2)

ENTRY(read_sctlr_el2)
	mrs x0,sctlr_el2
    ret
ENDPROC(read_sctlr_el2)


ENTRY(truly_get_hcr_el2)
    mrs     x0, hcr_el2
    ret
ENDPROC(truly_get_hcr_el2)

ENTRY(truly_set_mdcr_el2)

	mrs	x0, tpidr_el2

	kern_hyp_va  x0	// grab tvm
    ldr		x1, [x0, #TP_MDCR_EL2]
	msr		mdcr_el2, x1
	ret
ENDPROC(truly_set_mdcr_el2)

ENTRY(truly_test)
	mov x1, #65
	kern_hyp_va x0

	str x1, [x0]
	ldr x0, [x0]
	ret
ENDPROC(truly_test)


ENTRY(truly_run_vm)

	pop	lr, xzr
	mov	x4, lr		// save the link register of EL1 before losing it.
	kern_hyp_va  x0	// grab tvm
	msr	tpidr_el2, x0	// save tvm context


	ldr	x1, [x0, #TP_HCR_EL2]
  	msr     hcr_el2, x1

 //	ldr 	x1, [x0, #TP_HSTR_EL2]
//	msr 	hstr_el2, x1

   	ldr	x1, [x0, #TP_VTCR_EL2]
	msr	vtcr_el2, x1
 
     ldr     x1, [x0, #TP_VTTBR_EL2]
  	msr 	vttbr_el2, x1

   	ldr	x1, [x0, #TP_MDCR_EL2]
	msr	mdcr_el2, x1

	ldr	x1, [x0, #TP_SCTLR_EL2]
	msr	sctlr_el2, x1

	// Clear cntvoff for the host
	msr	cntvoff_el2, xzr



	mov	lr, x4
	eret // must eret here. stack pointer is changed 
ENDPROC(truly_run_vm)

/*
 * We enter here when the user executed in EL2 and performed an ilegal
 * command, such as svc or mmu abort
 * we pad the code back ,move to program counter ot previous position
 * and return to EL0
*/
EL2_sync:

	save_registers

	mrs x0, tpidr_el2
	mrs x1, tpidr_el0
	ldr x2,[x0,#TP_TPIDR_EL0]
	cmp x2,x1
	b.ne 3f // check context . is it the same thread ?

	mrs	x1, esr_el2
	lsr	x2, x1, #ESR_ELx_EC_SHIFT // Syndrom shifts 26 bits
	cmp x2, #ESR_ELx_EC_DABT_CUR  // 0x25 , MMU data abort in EL2
	b.eq 2f
	cmp x2, #ESR_ELx_EC_IABT_CUR  // 0x21 , MMU instruction abort in EL2
	b.eq 2f
	cmp x2, #ESR_ELx_EC_IABT_LOW
	b.eq 2f
	cmp x2, #ESR_ELx_EC_SVC64
	b.ne 2f
// svc
	mrs x0, tpidr_el2
	mrs x3, elr_el2
	str x3, [x0, #TP_SAVE_CMD]
	str x3, [x0, #TP_ELR_EL2]

	ldr x1, =truly_pad
	kern_hyp_va x1
	blr x1
	bl pad_invalidate

	mrs x3, elr_el2
	sub x3, x3, #4
	msr elr_el2, x3
// Prepare spsr_el2 to move to EL0h
//
	mrs x1,spsr_el2
    and w1,w1,#0xFFFFFFF0
    msr spsr_el2,x1

	restore_registers
	eret
2:
	mrs x0, tpidr_el2
	mrs x3, elr_el2
	str x3, [x0, #TP_SAVE_CMD]
	str x3, [x0, #TP_ELR_EL2]
	mrs x3, far_el2
	str x3, [x0, #TP_FAR_EL2]
	ldr x1, =truly_pad
	kern_hyp_va x1
	blr x1
	bl pad_invalidate
/*
 * update faulting address in array faulted addresses
*/
	ldr x1,=el2_prep_page_fault
	kern_hyp_va x1
	mrs x0,tpidr_el2
	blr x1
/*
 * if return 0 then just exit to EL0
*/
	cmp x0,xzr
	b.eq 4f

/*  else, the address was already fauled in user space, so
 *	exit to EL1 to map fault the address in EL1 and EL2
*/
// First , stash USR stack
	mrs x0, tpidr_el2
	mrs x1, sp_el0
	str x1,[x0, #TP_SP_EL0_USR]
 //   then modify sp_el0 to reflect kernel space sp_el0 (see get_current)
	ldr x1,[x0, #TP_SP_EL0_KRN]
	msr	sp_el0, x1

	ldr x0, =el2_mmu_fault_uaddr
	msr elr_el2, x0

	mrs x1, spsr_el2
	and w1, w1, #0xFFFFFFF0
	mov x0, #5
   	orr x1, x1, x0
	msr spsr_el2,x1
	eret

3:
	ldr x1, =truly_pad
	kern_hyp_va x1
	blr x1
	bl pad_invalidate
	restore_registers
	eret

4:
	restore_registers
/*
 * force ELx stack
*/
	msr cntvoff_el2 ,x1
	mrs x1, spsel
	orr w1, w1, 0x1
	msr spsel, x1
/*
* just fix spsr_el2 to EL0 and eret
*/
	mrs x1, spsr_el2
	and w1, w1, #0xFFFFFFF0
	msr spsr_el2, x1

	mrs  x1,cntvoff_el2
	msr  cntvoff_el2,xzr

	eret
ENDPROC(EL2_sync)

/*
 * addresses are mapped, Now try to run again in EL0
*/
ENTRY(el2_mmu_fault_bh)

	pop	lr, xzr			// restore stack to its previosu position

	mrs x0,tpidr_el2
	ldr x1,[x0, #TP_ELR_EL2]
	msr elr_el2,x1

// reflect sp_el0 of user space
	ldr x1,[x0, #TP_SP_EL0_USR]
	msr	sp_el0,x1

	bl pad_invalidate

	restore_registers	// restore EL2_sync
//
// force ELx stack
//
	msr cntvoff_el2 ,x1
	mrs x1, spsel
	orr w1, w1, 0x1
	msr spsel, x1
//
// just fix spsr_el2 to EL0 and eret
//
	mrs x1, spsr_el2
	and w1, w1, #0xFFFFFFF0
	msr spsr_el2, x1

	mrs  x1,cntvoff_el2
	msr  cntvoff_el2,xzr

	eret
ENDPROC(el2_mmu_fault_bh)

ENTRY(tp_call_hyp)
    hvc     #0
    ret
ENDPROC(tp_call_hyp)

// called in EL2
// x0 - va to flush
ENTRY(tp_flush_tlb)
	tlbi vae2, x0
	dsb sy
	ret
ENDPROC(tp_flush_tlb)

// x0 - vaddr
// x1 - size
ENTRY(tp_clear_icache)

        push x1,x0
        push x2,x3

        mrs     x3, ctr_el0
        ubfx    x3, x3, #16, #4
        mov     x3, #4 // x2=word size
        lsl     x2, x2 ,x3 // x2 cache line size

        add     x1, x0, x1 // end

1:      ic      ivau, x0   // clean & invalidate I line / unified line
        dsb 	ish
        add     x0, x0, x2
        cmp     x0, x1
        b.lo    1b

        pop x2,x3
        pop x1,x0
        isb
        ret
ENDPROC(tp_clear_icache)

// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.den0024a/CHDEEDDC.html
.align 11
ENTRY(__truly_vectors)
        ventry EL2_sync                        //Current EL with SP0
        invalid_vector  EL2_irq_invalid,#2                 // IRQ EL2t
        invalid_vector  EL2_fiq_invalid,#3                // FIQ EL2t
        invalid_vector  EL2_error_invalid,#4             // Error EL2t

        invalid_vector  EL2_syncELSpx,#5  	              // Current EL with SPx
        invalid_vector  EL2_irq_invalidELSpx,#6                 // IRQ EL2h
        invalid_vector  EL2_fiq_invalidELSpx,#7                 // FIQ EL2h
        invalid_vector  EL2_error_invalidELspx,#8               // Error EL2h

        ventry  EL1_sync                        			// Synchronous 64-bit EL1
        invalid_vector  EL1_irq,#14                       				// IRQ 64-bit EL1
        invalid_vector  EL1_fiq_invalid ,#9                // FIQ 64-bit EL1
        invalid_vector  EL1_error_invalid  ,#10             // Error 64-bit EL1

        ventry  EL1_irq  	              							// Synchronous 32-bit EL1
        invalid_vector  EL1_irq_invalid,#11                 		// IRQ 32-bit EL1
        invalid_vector  EL1_fiq_invalidLowEL32 ,#12               // FIQ 32-bit EL1
        invalid_vector  EL1_error_invalidLowEL32,#13               // Error 32-bit EL1
ENDPROC(__truly_vectors)

.popsection
