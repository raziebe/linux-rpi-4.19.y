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
* echo out > /sys/class/gpio/gpio475/direction
* echo 475 > /sys/class/gpio/export
* echo in > /sys/class/gpio/gpio475/direction
*/
struct hyp_wait hypeve;

static int gpio_w = 485;
module_param(gpio_w, int, 0 );

static int gpio_r = 475;
module_param(gpio_r, int, 0);

static int cpu = 1;
module_param(cpu, int, 0);

void trig_off(void)
{
	gpio_set_value(gpio_w, 0);
}

void wait_for_blackness(void)
{
	int rc;

write_again:
	trig_off();
	rc = gpio_get_value(gpio_r);
	if (rc == 1)
		goto write_again;	
}

static void offlet_trigger(struct hyplet_vm *hyp, struct hyp_wait *hypevent)
{
	int i = 0;
	long t1 = 0 ,t2 = 0;
	int rc = 0;
	
	hyp->user_arg1 =0;
	hyp->user_arg2 =0;

	wait_for_blackness();
// trigger on.
	t1 = cycles_ns();
	gpio_set_value(gpio_w, 1);

	for (i = 0; i < 100000 && rc == 0; i++){
		rc = gpio_get_value(gpio_r);
		if (rc==0)
			udelay(50);
	}
	t2 = cycles_ns();
	hyp->user_arg1 = t1;
	hyp->user_arg2 = t2;
	hyp->user_arg3 = rc;
}

static int offlet_init(void)
{
	hypeve.offlet_action =  offlet_trigger;

	printk("offlet:  trigger on cpu %d\n"
			"gpios: %d %d\n",
			cpu, gpio_w, gpio_r);

//	gpio_request(gpio_w,"gpio485");
//	gpio_request(gpio_r,"gpio475");
//	gpio_direction_input(gpio_r);	
//	gpio_direction_output(gpio_w, 0);	

	offlet_register(&hypeve, cpu);

	return 0;
}

static void offlet_cleanup(void) 
{
//	gpio_free(gpio_w);
//	gpio_free(gpio_r);

	offlet_unregister(&hypeve,cpu);
	printk( "offlet exit\n");
}

module_init(offlet_init);
module_exit(offlet_cleanup);

MODULE_DESCRIPTION("offlet gpio");
MODULE_AUTHOR("Raz Ben Jehuda");
MODULE_LICENSE("GPL");
