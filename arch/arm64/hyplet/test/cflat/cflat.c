#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>

#include "../utils/hyplet_user.h"
#include "../utils/hyplet_utils.h"

#define NR_OPCODES	100

int func_id = 8;

__attribute__ ((section("hyp_rw")))  unsigned int cnt = 0;
__attribute__ ((section("hyp_rw")))  unsigned long instr[NR_OPCODES][3] = {0};

__attribute__ ((section("hyp_rx")))  long record_opcode(long fault_addr,long a,long b)
{
	instr[cnt][0] = fault_addr;
	instr[cnt][1] = a;
	instr[cnt++][2] = b;
	return 0;
}

__attribute__ ((section("hyp_x"))) long  user_log(unsigned long instr,long type) 
{
	return 0;
}

/*
 * This code is executed in an hyplet context
 */
static int hyplet_start(int argc, char *argv[])
{
	int rc;
	int cpu = -1;
	int stack_size = sysconf(_SC_PAGESIZE) * 2;
	void *stack_addr;

	long hyp_sec_x;
	int  hyp_sec_x_size;

	long hyp_sec_rw;
	int  hyp_sec_rw_size;

	long hyp_sec_rx;
	int hyp_sec_rx_size;

  	if (Elf_parser_load_memory_map(argv[0])){
		printf("Failed to ELF parser\n");
		return -1;
  	}

	if (!get_section_addr("hyp_x", &hyp_sec_x, &hyp_sec_x_size)) {
		fprintf(stderr, "hyp_x: \n");
		return -1;
	}	
	
	if (!get_section_addr("hyp_rw", &hyp_sec_rw, &hyp_sec_rw_size)) {
		fprintf(stderr, "hyp_rw:\n");
		return -1;
	}	

	if (!get_section_addr("hyp_rx", &hyp_sec_rx, &hyp_sec_rx_size)) {
		fprintf(stderr, "hyp_rw:\n");
		return -1;
	}

	
	if (hyplet_map_vma((void *)hyp_sec_rw, cpu)) {
		fprintf(stderr, "hyplet: Failed to hyp_rw\n");
		return -1;
	}


	if (hyplet_map_vma((void*)hyp_sec_x, cpu)) {
		fprintf(stderr, "hyplet: Failed to map sec_x\n");
		return -1;
	}

	if (hyplet_map_vma((void*)hyp_sec_rx, cpu)) {
		fprintf(stderr, "hyplet: Failed to map sec_x\n");
		return -1;
	}

	rc = posix_memalign(&stack_addr,
			    sysconf(_SC_PAGESIZE), stack_size);
	if (rc < 0) {
		fprintf(stderr, "hyplet: Failed to allocate a stack\n");
		return -1;
	}
//
// must fault the stack
//
	memset(stack_addr, 0x00, stack_size);
	printf("Setting stack at %p\n", stack_addr);
	if (hyplet_set_stack(stack_addr, stack_size, cpu)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_set_stack(stack_addr, stack_size, cpu)) {
		fprintf(stderr, "hyplet: Failed to set a stack\n");
		return -1;
	}

	if (hyplet_set_callback(record_opcode, cpu)) {
		fprintf(stderr, "hyplet: Failed to map test opcode\n");
		return -1;
	}

	if (hyplet_rpc_set(record_opcode, func_id, cpu)) {
		fprintf(stderr, "hyplet: Failed to assign opcode\n");
		return -1;
	}

	fprintf(stderr, "hyplet: set mdcr on XXXXXXX\n");
	return 0;
}

/*
 * it is essential affine the program to the same 
 * core where it runs.
*/
int main(int argc, char *argv[])
{
    int rc;
    int i;

    if (hyplet_start(argc, argv)) {
	return -1;
    }

    printf("\nHyplet set. Prepare to run trap\n");
    while(cnt == 0){
	sleep(1);
    }

    for (i = 0; i < cnt ; i++)
   	 printf("DATA %d) %lx,%lx,%lx\n", 
			instr[i][0],   instr[i][1],  instr[i][2] );
}
