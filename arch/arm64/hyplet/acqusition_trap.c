#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/hyplet.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>		/* for file_operations */
#include <linux/slab.h>	/* versioning */
#include <linux/cdev.h>
#include "acqusition_trap.h"
#include "hyp_mmu.h"
#include "hypletS.h"

unsigned long __hyp_text __hyp_phys_to_virt(unsigned long addr,struct hyplet_vm *vm)
{
	return ( (unsigned long)((addr) - vm->hyp_memstart_addr) | PAGE_OFFSET );
}

unsigned long __hyp_text hyp_phys_to_virt(unsigned long addr,struct hyplet_vm *vm)
{
	if (is_hyp())
		return KERN_TO_HYP( __hyp_phys_to_virt(addr, vm) - KERN_TO_HYP(0)) & HYP_PAGE_OFFSET_MASK;
	return (unsigned long) phys_to_virt(addr);
}


static inline long make_special_page_desc(unsigned long real_phyaddr,int s2_rw)
{
	unsigned long addr = real_phyaddr;

	return (DESC_AF) | (0b11 << DESC_SHREABILITY_SHIFT) |
	                ( s2_rw  << DESC_S2AP_SHIFT) | (0b1111 << 2) |
	                  DESC_TABLE_BIT | DESC_VALID_BIT | addr;
}

/*
 * Given a physical address, search in the page table
 * and find the page descriptor and change its access rights
 */
unsigned long*  __hyp_text  ipa_find_page_desc(struct hyplet_vm *vm,unsigned long phy_addr)
{
	int i,j ,k;
	unsigned long temp;
	unsigned long *desc1;
	unsigned long *desc2;
	unsigned long *desc3;

	desc1  = vm->vttbr_el2_kern;
	if (is_hyp())
		desc1  = (unsigned long*)KERN_TO_HYP(vm->vttbr_el2_kern);

	i = phy_addr / 0x40000000; // 1GB
	temp = desc1[i]  & 0x000FFFFFFFFFFC00LL;

	desc2 = (unsigned long *) hyp_phys_to_virt(temp, vm);
	j = (phy_addr & 0x3FFFFFFF) / 0x200000; // 2MB
	temp = desc2[j]  & 0x000FFFFFFFFFFC00LL;

	desc3 = (unsigned long *) hyp_phys_to_virt(temp, vm);
	k = (phy_addr & 0x1FFFFF) / PAGE_SIZE;

	return &desc3[k];
}

/*
 * Call in EL2 context.
 * Walk on the page table and set each page to readonly
 */
void __hyp_text   walk_ipa_el2(struct hyplet_vm *vm,int s2_page_access)
{
	int i,j ,k, n;
	unsigned long *desc0 = (unsigned long *)KERN_TO_HYP(vm->ipa_desc_zero);
	unsigned long temp;
	unsigned long *desc1;
	unsigned long *desc2;
	unsigned long *desc3;

	vm->ipa_pages = 0;

	for (i = 0 ; i < PAGE_SIZE/sizeof(long); i++){

		if (desc0[i]) {
			temp  = desc0[i] & 0x000FFFFFFFFFFC00LL;
			desc1 = (unsigned long *) hyp_phys_to_virt(temp, vm);

			for (j = 0 ; j < PAGE_SIZE/sizeof(long); j++){
				if (desc1[j]){
					temp = desc1[j] & 0x000FFFFFFFFFFC00LL;
					desc2 = (unsigned long *)hyp_phys_to_virt(temp, vm);

					for (k = 0 ; k < PAGE_SIZE/sizeof(long); k++){

						if (desc2[k]){

							temp = desc2[k] & 0x000FFFFFFFFFFC00LL;
							desc3 = (unsigned long *)  hyp_phys_to_virt(temp, vm);

							for (n = 0 ; n < PAGE_SIZE/sizeof(long); n++){
								if (desc3[n]){
									/*
									 * set page access rights to S2_RW.
									 * */
									temp = desc3[n] & 0x000FFFFFFFFFFC00LL;
									desc3[n] = make_special_page_desc(temp, s2_page_access);
									vm->ipa_pages++;
								}
							}
						}
					}
				}
			}
		}
	}
}


#define RAMSTR "System RAM"

int __hyp_text is_black_listed(long pfn)
{
	return ((pfn <= 34940 && pfn >= 34935) || (pfn > 242688) || (pfn < 0));
}

static void map_range_to_el2(struct resource * res) 
{
    int rc;

    resource_size_t i, is = PAGE_SIZE;
    long pfn;
    struct page * p;

    printk("Mapping Ram Range [0x%llx,0x%llx] size %d\n", 
	res->start,res->end, (int)(res->end - res->start));

    for (i = res->start  ; i < res->end ; i += is) {
	pfn = (i >> PAGE_SHIFT);
	
	if (is_black_listed(pfn)) {
		printk("Skipping pfn %ld address %p\n",pfn,(unsigned long *)i);
		continue;
	}

        p = pfn_to_page((i) >> PAGE_SHIFT);
        is = min((size_t) PAGE_SIZE, (size_t) (res->end - i + 1));

        if (is < PAGE_SIZE) {
            // We can't map partial pages and
            // the linux kernel doesn't use them anyway
            printk("Partial page: vaddr %p size: %lu", (void *) i, (unsigned long) is);
        } 
		else{

			rc = hyp_map_physical((unsigned long *)(i),
					(unsigned long *)(i + PAGE_SIZE), PAGE_HYP);
			if (rc < 0 ){
				printk("Failed to hyp map page = %lld\n",i);
			}
	   
        }
    }
}

void map_system_ram_to_hypervisor(void)
{
    struct resource *p;

    for (p = iomem_resource.child; p ; p = p->sibling) {
        if (strcmp(p->name, RAMSTR))
            continue;
        map_range_to_el2(p);
    }
}

/* Lime's stuff */
void __hyp_text hyp_memcpy(char *t,char *s, int size)
{
	int i;
	
	for (i = 0 ; i < size; i++)
		t[i] = s[i];
}

/*
 * Called in EL2 to handle a faulted address
 */
unsigned long __hyp_text hyplet_handle_abrt(struct hyplet_vm *vm,
		unsigned long phy_addr)
{
	unsigned long* desc;
	long pfn;
	struct LimePagePool* lp = NULL;

	// First clean the attributes bits: address is in bits 47..12
	phy_addr &= 0xFFFFFFFFF000;

	// Find the descriptor in the MMU
	desc = ipa_find_page_desc(vm, phy_addr);

	// Return descriptor to its RW
	*desc = make_special_page_desc(phy_addr, S2_PAGE_ACCESS_RW);
	hyplet_invld_ipa_tlb(phy_addr >> 12);
	vm->ipa_pages_processed++;

	if (is_device_mem(vm, phy_addr)){
		return 0x99;
	}

	if(!vm->limePool)
		return -1;

	//if(vm->limePool->size + 1 >= POOL_SIZE)
	//	return 0;

	lp  = (struct LimePagePool *) KERN_TO_HYP(vm->limePool);
	pfn = (phy_addr >> PAGE_SHIFT);

	// copy page content into pool
	if (!is_black_listed(pfn)){
		int index = 0;
		struct LimePageContext* slot;
		unsigned char *p = (unsigned char *)phy_addr;

		vm->cur_phy_addr = phy_addr;

		/* spin locking the page pool */
		//hyp_spin_lock(&lp->lock);

		/* find empty slot in pool */
		for (; index < POOL_SIZE; index++)
			if(lp->pool[index].state == LiME_POOL_PAGE_FREE)
				break;

		/* no empty slot was found */
		if(index >= POOL_SIZE)
		{
			vm->pool_page_overwrite_counter++;
			/* default to index 0 */
			index = 0;
		}

		slot = &( lp->pool[index] ); 

		slot->phy_addr = phy_addr;
		slot->state = LiME_POOL_PAGE_OCCUPIED;

		hyp_memcpy((char *)KERN_TO_HYP(slot->hyp_vaddr), p, PAGE_SIZE);

		/* unlocking the lime pool */
		//hyp_spin_unlock(&lp->lock);
	}

	return 0;
}



/* Called From LiME right before turning on the acqusion */
int setup_lime_pool(void)
{
	int cpu;
	int rc;
	int i;
	struct LimePagePool* limepool = NULL;
	struct hyplet_vm *vm;
	struct page *pg;

	limepool = kmalloc( sizeof(struct LimePagePool), GFP_KERNEL);
	if(!limepool)
	{
		printk("Limepool kmalloc failed");
		return -1;
	}
	memset(limepool, 0x00, sizeof(struct LimePagePool));

	// limepool->lime_current_place = 0; // Convert to atomic

	hyp_spin_lock_init(&limepool->lock);

	for_each_possible_cpu(cpu){
		limepool = kmalloc( sizeof(struct LimePagePool), GFP_KERNEL);
		if(!limepool)
		{
			printk("Limepool kmalloc failed");
			return -1;
		}
		memset(limepool, 0x00, sizeof(struct LimePagePool));
		hyp_spin_lock_init(&limepool->lock);

		vm = hyplet_get(cpu);
		vm->limePool = limepool;

		rc  = create_hyp_mappings(limepool, limepool + sizeof(*limepool), PAGE_HYP);
		if (rc){
			printk("Cannot map limepool\n");
			return -1;
		}

		/* Allocate space for each page in the pool */
		for (i = 0 ; i < POOL_SIZE; i++) {
			void *v;

			pg = alloc_page(GFP_KERNEL);
			v =  kmap(pg);
			rc  = create_hyp_mappings(v, v + PAGE_SIZE - 1, PAGE_HYP);
			if (rc){
				printk("Cannot map hyp %d \n",i);
			}

			/* Put pointer to allocated page in the pool */
			limepool->pool[i].hyp_vaddr = (long *)v;
		}
	}

	return 0;
}	
EXPORT_SYMBOL_GPL(setup_lime_pool);


/* user interface  */
static struct proc_dir_entry *procfs = NULL;

void turn_on_acq(void)
{
	struct hyplet_vm *vm = hyplet_get_vm();

	walk_on_mmu_el1();
	map_system_ram_to_hypervisor();
	if (setup_lime_pool()){
		printk(KERN_EMERG "SETUP LIME POOL FAILED\n");
		return;
	}
	printk(KERN_EMERG "Marking all pages RO\n");
	printk(KERN_EMERG "Fnished turn_on_acq\n");
	
	hyplet_call_hyp((void *)KERN_TO_HYP(walk_ipa_el2), KERN_TO_HYP(vm),
			S2_PAGE_ACCESS_R);
}
EXPORT_SYMBOL_GPL(turn_on_acq);

static ssize_t proc_write(struct file *file, const char __user * buffer,
			  size_t count, loff_t * dummy)
{
	printk("Updating MMU\n");
	turn_on_acq();
	return count;
}


static int proc_open(struct inode *inode, struct file *filp)
{
	filp->private_data = (void *)0x01;
	return 0;
}

static ssize_t proc_read(struct file *filp, char __user * page,
			 size_t size, loff_t * off)
{
	ssize_t len = 0, i = 0;
	int cpu;
	long phy_addr = -1;
	struct LimePageContext* pool_min;

	if (!filp->private_data)
		return 0;

	for_each_possible_cpu(cpu){
		struct hyplet_vm *vm;

		vm = hyplet_get(cpu);
		len += sprintf(page + len,
				"%d: pages processed = %d phyaddr = %lx pool_page_overwrite = %d\n",
				cpu,
				vm->ipa_pages_processed,
 				vm->cur_phy_addr,
				vm->pool_page_overwrite_counter);
	}

	if (hyplet_get_vm()->limePool) { 
		for (; i < POOL_SIZE; i++)
		{
			pool_min = &( hyplet_get_vm()->limePool->pool[i] );
			
			if (pool_min != NULL)
				phy_addr = pool_min->phy_addr;

			len += sprintf(page + len, 
					"page[%d]: phy_addr = %lx, state = %d\n",
					i,
					phy_addr,
					pool_min->state);
		}
	}
	filp->private_data = 0x00;
	return len;
}


static struct file_operations acqusition_proc_ops = {
	.open = proc_open,
	.read = proc_read,
	.write = proc_write,
};


void acqusion_init_procfs(void)
{
	procfs = proc_create_data("hyplet_stats", 
			O_RDWR, NULL, &acqusition_proc_ops, NULL);
}



