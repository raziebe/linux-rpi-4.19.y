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
* echo 475 > /sys/class/gpio/export
* echo out > /sys/class/gpio/gpio475/direction
*/
static int toggle = 0;
struct hyp_wait hypeve;

static int gpio;
module_param(gpio, int, 0);

static int cpu;
module_param(cpu, int, 0) ;

static int dir;
module_param(dir, int, 0) ;

static void offlet_cleanup(void) 
{
	offlet_unregister(&hypeve,cpu);
	printk( "driver exit\n");
}

/*
 * callback in an offlet context 
*/
static void write_gpio(struct hyplet_vm *hyp,struct hyp_wait* hypevent)
{
	gpio_set_value(gpio, toggle);
	toggle = !toggle;
}

static void read_gpio(struct hyplet_vm *hyp,struct hyp_wait* hypevent)
{
	int rc;
	rc = gpio_get_value(gpio);
	hyp->user_arg1 = rc;		
}

static int offlet_init(void)
{
	if (dir == 0) 
		hypeve.offlet_action =  write_gpio;
	else
		hypeve.offlet_action =  read_gpio;
	
	printk("GPIP: action %s \n",dir == 0 ? "write" : "read");
	offlet_register(&hypeve, cpu);

	return 0;
}

module_init(offlet_init);
module_exit(offlet_cleanup);

MODULE_DESCRIPTION("offlet gpio");
MODULE_AUTHOR("Raz Ben Jehuda");
MODULE_LICENSE("GPL");
