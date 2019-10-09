#define _GNU_SOURCE
 
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <linux/hyplet_user.h>

#include "hyplet_utils.h"

#define USONIC_TRIG	19
#define USONIC_ECHO	29

// States
#define  USONIC_ECHO_START	4
#define  USONIC_ECHO_END	5
#define  USONIC_TRIG_START	6
#define  USONIC_TRIG_END	7
#define  USONIC_BIT_DONE	8
#define  USONIC_GPIO_NR		3

static int usonic_gpio_idx_max = 0;
static int usonic_echo_gpio[USONIC_GPIO_NR] = {-1};
static int usonic_trig_gpio[USONIC_GPIO_NR] = {-1};
static int usonic_mode  = USONIC_ECHO;
static int iter = 1;
static int bit1_delay = 0;
static int bit0_delay = 0;
static int cpu = -1;
static int run = 1;
static int usonic_state = 0;
static int bit = 0;
static long echo_start_ns = 0;
static long echo_end_ns = 0;
float supersonic_speed_us = 0.0343;// centimeter/microsecond;	

static int get_trig_gpio(void)
{
	static int curr_gpio_idx = 0;

	curr_gpio_idx = (curr_gpio_idx + 1) % usonic_gpio_idx_max;
	return usonic_trig_gpio[curr_gpio_idx];
}

static int get_echo_gpio(void)
{	
	// We use a single echo.
	return usonic_echo_gpio[0];
}

/*
    Return value is broken to:
	long  cmd:8;  USONIC_ECHO/USONIC_TRIG
	long  cmd_val:8; // echo/trig 1 or 0
	long  gpio:8;
	long  pad:40;	
*/
long usonic_trig(long time_ns)
{
	long end = 0;
	long rc;
	int g = get_trig_gpio() << 16;

	if (usonic_state == USONIC_TRIG_START){
		char cmd = USONIC_TRIG;
		short cmd_val = ((short)1 << 8) ;

		rc = cmd_val | cmd | g;
		
		// Start bit transmit
		usonic_state = USONIC_TRIG_END; 
		return rc;
	}

	if (usonic_state == USONIC_TRIG_END){
		//  End the transmit, according to bit value
		if (bit == 0)
			end = time_ns + bit0_delay;
		if (bit == 1)
			end = time_ns + bit1_delay;

		usonic_state = USONIC_TRIG_START; 
		while (hyp_gettime() < end && run);
		rc = USONIC_TRIG | g;
		return   rc;// End Bit transmit
	}
	hyp_print("usonic_trig: should not be here\n");
	return -1;
}

long usonic_echo(long time_ns)
{
	long end = 0;
	long rc;
	int g = (get_echo_gpio() << 16);

	if (!run)
		return 0;

	if (usonic_state == USONIC_ECHO_START){
		usonic_state = USONIC_ECHO_END;
		// wait untill echo changes to ECHO 1
		rc =  USONIC_ECHO | g;
		return rc;
	}

	if (usonic_state == USONIC_ECHO_END){
		char cmd = USONIC_ECHO;
		short cmd_val = ((short)1 << 8) ;
		// Save the time of transition from 0 to 1
		echo_start_ns = time_ns;
		// wait until echo would reset back to 0, bug : wait forever ?
		rc = cmd_val | cmd | g;
		usonic_state = USONIC_BIT_DONE;

		return rc;
	}

	if (usonic_state == USONIC_BIT_DONE) {
		float distance;
		long dt;
		// Save the time of transition from 1 to 0
		echo_end_ns = time_ns;
		dt = (echo_end_ns - echo_start_ns)/1000;
		distance  = ((float)dt * supersonic_speed_us)/2;

		hyp_print("#%d us = %ld distance=%f bit=%d\n", 
			iter++, dt, distance, bit);
	//	bit = !bit; /* 1010101...*/
		usonic_state = USONIC_TRIG_START;
	}

	/*
	 * We use HC-RS04 . To be able to wait for an echo, we must trig 
	 * before echo.  We trig without any delays.
	*/
	if (usonic_state == USONIC_TRIG_START){
		char cmd = USONIC_TRIG;
		short cmd_val = ((short)1 << 8) ;
		
		g =  get_trig_gpio() << 16;

		rc = cmd_val | cmd | g;
		
		// Start bit transmit
		usonic_state = USONIC_TRIG_END; 
		return rc;
	}

	if (usonic_state == USONIC_TRIG_END){
		//  End the transmit
		usonic_state = USONIC_ECHO_START; 

		g =  get_trig_gpio() << 16;
		rc = USONIC_TRIG | g;
		return   rc; // End Bit transmit
	}

	hyp_print("echo should not be here\n");
	return -1;
}


/* Local Send/Receive Tester
 * The kerne offlet act upton our command.
 * It may trigger an echo or end an echo.
 * It may start an echo read.
*/
long  usonic_transducer(long time_ns,long a2,long a3,long a4)
{
	switch(usonic_mode)
	{
		case USONIC_TRIG:
			return usonic_trig(time_ns);
		case USONIC_ECHO:
			return usonic_echo(time_ns);
	}
	hyp_print("Usonic ilegal mode\n");
	return 0;
}

static int hyplet_start(void)
{
	int rc;
	int stack_size = sysconf(_SC_PAGESIZE) * 50;
	void *stack_addr;

	/*
	 * Create a stack
	 */
	rc = posix_memalign(&stack_addr,
			    sysconf(_SC_PAGESIZE), stack_size);
	if (rc < 0) {
		fprintf(stderr, "hyplet: Failed to allocate a stack\n");
		return -1;
	}
// must fault it
	memset(stack_addr, 0x00, stack_size);
	if (hyplet_map_all(cpu)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}
	
	if (hyplet_set_stack(stack_addr, stack_size, cpu)) {
		fprintf(stderr, "hyplet: Failed to map a stack\n");
		return -1;
	}

	if (hyplet_assign_offlet(cpu, usonic_transducer)) {
		fprintf(stderr, "hyplet: Failed to map code\n");
		return -1;
	}

	return 0;
}


int open_gpio(int _gpio,char *dir)
{
	int fd;
	int b;
	char gpio[128];
	char* export = "/sys/class/gpio/export";

	fd = open(export, O_WRONLY);
	if (fd < 0){
		perror("Failed to open gpio485 file");
		return -1;
	}

	sprintf(gpio,"%d",_gpio);
	b = write(fd, gpio, strlen(gpio));
	if (b < 0){
		perror("write:");
		return -1;
	}
	close(fd);

	sprintf(gpio, "/sys/class/gpio/gpio%d/direction",_gpio);

	fd = open(gpio, O_WRONLY);
	if (fd < 0){
		perror("Failed to open %d file");
		return -1;
	}

	b = write(fd, dir, strlen(dir));
	if (b < 0){
		perror("write:");
		return -1;
	}
	close(fd);
	return 0;
}


int help(int argc, char *argv[])
{
        printf("%s -c <cpu> -u [bit delay us] -m <mode>(TRIG,ECHO) -t <trig gpio> -e <echo gpio>\n",
			argv[0]);
	exit(0);
}

int parse_opt(int argc, char *argv[])
{
	int opt;
	int ret = -1;
	char mode[16];

	while ((opt = getopt(argc, argv, "m:c:u:t:e:")) != -1) {
		switch (opt) {

		case 'm':
		    strcpy(mode, optarg);
		    break;

		case 'c':
		    cpu = atoi(optarg);
		    break;

		case 'u':
		    bit0_delay = atoi(optarg);
		    break;

		case 't':
		     usonic_trig_gpio[usonic_gpio_idx_max++] = atoi(optarg);
		    break;

		case 'e':
		     usonic_echo_gpio[0] = atoi(optarg);
		    break;

		default: /* '?' */
			help(argc,argv);
    		}
    }
    
    if (cpu <= 0 ){
	printf("No cpu %d\n", cpu);
	return -1;
    }	

    if (!strcasecmp(mode,"trig"))
	usonic_mode = USONIC_TRIG;    

    if (!strcasecmp(mode,"echo"))
	usonic_mode = USONIC_ECHO;    
 
    if (usonic_mode != USONIC_TRIG && usonic_mode != USONIC_ECHO){
	printf("Ilegal mode %s\n", mode);
	help(argc, argv);
    }

    if (usonic_mode == USONIC_TRIG) {
	int i;

	if (bit0_delay < 0) {
		printf("must provide a sane bit delay");
		help(argc, argv);
	}
	bit0_delay *= 1000; // to nanosecond
	bit1_delay = bit0_delay;
	usonic_state = USONIC_TRIG_START;
	for (i = 0 ; i < usonic_gpio_idx_max; i++) {
		ret = open_gpio(usonic_trig_gpio[i], "out");
		if (ret) {
			printf("Failed to program %d gpio\n",
				usonic_trig_gpio[i]);
			return -1;
		}
	}
    	printf("Drop cpu %d delay %dus, %d gpios, mode=%s\n",
		cpu, bit0_delay/1000, usonic_gpio_idx_max ,mode);
    }

    if (usonic_mode == USONIC_ECHO) {
		
		int i;

		ret = open_gpio(usonic_echo_gpio[0], "in");
		if (ret) {
			printf("Failed to program %d gpio\n",	
				usonic_echo_gpio[0]);
			return -1;
		}

		ret = open_gpio(usonic_trig_gpio[0], "out");
		if (ret) {
			printf("Failed to program %d gpio\n",	
				usonic_trig_gpio[0]);
			return -1;
		}

		usonic_state = USONIC_TRIG_START; // HC-SR04 patch. To echo, we need to trig.
		printf("Drop cpu %d,  echo gpio %d,%d mode=%s\n",
			cpu, usonic_echo_gpio[0], usonic_trig_gpio[0] ,mode);
	}

   return ret;
}

/*
 * it is essential affine the program to the same 
 * core where it runs.
*/
int main(int argc, char *argv[])
{
    int i;
    int rc;

    if (parse_opt(argc, argv) ){
	help(argc, argv);
    }

    if (hyplet_drop_cpu(cpu) < 0 ){
	printf("Failed to drop processor\n");
	return -1;
    }

    hyplet_start();
 
    while (1) {
	    print_hyp();
	    usleep(10000);
    }
    run  = 0;
    sleep(1);
}

