#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/hyplet.h>


/*
 * returns the the hyplet address if exists
 */
unsigned long __hyp_text get_hyplet_addr(int hyplet_id,struct hyplet_vm * hyp)
{
	if (hyp->hyplet_id == 0)
			return UL(0);
	if (hyp->hyplet_id != hyplet_id)
			return 0L;
	return hyp->user_hyplet_code;
}
