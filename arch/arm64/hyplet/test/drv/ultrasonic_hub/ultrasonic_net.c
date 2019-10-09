#include <linux/module.h>
#include <linux/fs.h>		/* for file_operations */
#include <linux/version.h>	/* versioning */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/hyplet.h>

#include "utils.h"

/*
 * must export before use.
* and give a direction
* echo 485 > /sys/class/gpio/export
* echo in > /sys/class/gpio/gpio475/direction
* echo 475 > /sys/class/gpio/export
* echo out > /sys/class/gpio/gpio475/direction
*/
DEFINE_PER_CPU(struct hyp_wait ,HYPEVE);

void trig(int gpio, int val)
{
	gpio_set_value(gpio, val);
}

/*
 * Wait until we don't get this echo value
*/
long wait_echo(int gpio, int val)
{
	int rc;

read_again:
	rc = gpio_get_value(gpio);
	if (rc == val)
		goto read_again;
	return cycles_ns();
}

/*
    Return value is broken to:
	long  cmd:8;  USONIC_ECHO/USONIC_TRIG
	long  cmd_val:8; // trig 1 or 0
	lonf  gpio: 8;
	long  pad:40;
*/
static void offlet_trigger(struct hyplet_vm *hyp, struct hyp_wait *hypevent)
{
	int cmd =      (int) ( (hyp->user_arg1      )  & 0xFF );
	int cmd_val =  (int) ( (hyp->user_arg1 >> 8 )  & 0xFF ) ;
	int gpio    =  (int) ( (hyp->user_arg1 >> 16)  & 0xFF ) ;

	if (cmd == USONIC_TRIG) {
		trig(gpio, cmd_val);
		hyp->user_arg1 = cycles_ns();
		return;
	}

	if (cmd == USONIC_ECHO) {
		hyp->user_arg1 = wait_echo(gpio, cmd_val);
		return;
	}

	printk("should not be here.reset to TRIG %ld\n",hyp->user_arg1);
}

static int offlet_init(void)
{
	struct hyp_wait *hypeve;
	int cpu = 0;

	for_each_possible_cpu(cpu) {
		hypeve = &per_cpu(HYPEVE, cpu);
		hypeve->offlet_action =  offlet_trigger;
		printk("Offlet registered at cpu %d\n", cpu);
		offlet_register(hypeve, cpu);
	}
	return 0;
}

static void offlet_cleanup(void) 
{
	int cpu = 0;
	struct hyp_wait *hypeve;

	for_each_possible_cpu(cpu) {
		hypeve = &per_cpu(HYPEVE, cpu);
		offlet_unregister(hypeve, cpu);
	}
	printk( "offlet exit\n");
}

module_init(offlet_init);
module_exit(offlet_cleanup);

MODULE_DESCRIPTION("offlet gpio");
MODULE_AUTHOR("Raz Ben Jehuda");
MODULE_LICENSE("GPL");
