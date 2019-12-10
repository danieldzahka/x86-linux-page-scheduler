#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <linux/syscalls.h>
#include <linux/hugetlb.h>
#include <linux/kernel.h>
#include <linux/mm_types.h>
#include <linux/rwsem.h>
#include <linux/sched/mm.h>
#include <linux/pagewalk.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/page_ref.h>
#include <linux/gfp.h>

#include <pg_sched_mem.h>

#define MAX_INIT_VMAS 20
#define MAX_NEW_VMAS 20

/* static int num_vmas; /\* May purposely become stale *\/ */

static int init_vmas_size;
static struct vm_area_struct * init_vmas[MAX_INIT_VMAS];

static ktime_t kt;
static struct hrtimer timer;
static struct task_struct* scanner_thread = NULL;

static struct mm_struct* my_mm;

struct vma_desc {
    struct vm_area_struct * vma; /*Use as key*/
    unsigned long vm_start;
    unsigned long vm_end;
    int *         page_accesses;
    int           num_pages;
};

/* For Additional Anon VMAs */
static int new_vmas_size = 0;
static struct vma_desc new_vmas[MAX_NEW_VMAS];

void
free_page_access_arrays(void)
{
    int i;

    for (i = 0; i < new_vmas_size; ++i){
	kfree(new_vmas[i].page_accesses);
    }
}

/*Make sure we dont free the data before this*/
void
print_page_access_data(void)
{
    int i;
    int j;
    int pages;

    printk(KERN_INFO "vma,pfn,count\n");
    for (i = 0; i < new_vmas_size; ++i){
	for (j = 0; j < new_vmas[i].num_pages; ++j){
	    printk(KERN_INFO "%d,%d,%d\n", i, j, new_vmas[i].page_accesses[j]);
	}
    }
}

static int
track_vma(struct vm_area_struct * vma, int idx)
{
    struct vma_desc desc =
	{
	    .vma           = vma,
	    .vm_start      = vma->vm_start,
	    .vm_end        = vma->vm_end,
	    .page_accesses = NULL,
	    .num_pages     = 0,
	};
    int num_pages = (vma->vm_end - vma->vm_start) & ((1<<12) - 1) ?
	((vma->vm_end - vma->vm_start) >> 12) + 1 : (vma->vm_end - vma->vm_start) >> 12;
    desc.num_pages = num_pages;
    
    desc.page_accesses = kzalloc(num_pages * sizeof(int), GFP_KERNEL);
    if (IS_ERR(desc.page_accesses)){
	printk(KERN_EMERG "Error Allocating page access vector\n");
	return -1;
    }

    WARN_ON(idx >= MAX_NEW_VMAS);

    new_vmas[idx] = desc;
    return 0;
}

/* Return the idx of the region, store if not there */
/* Worry about merging VMA's later... */
static int
index_of_vma(struct vm_area_struct * vma)
{
    int i, status;
    for (i = 0; i < new_vmas_size; ++i){
	//make sure that the bounds are still valid...
	if (vma == new_vmas[i].vma){
	    if (vma->vm_start != new_vmas[i].vm_start ||
		vma->vm_end != new_vmas[i].vm_end){
		//resize
		kfree(new_vmas[i].page_accesses);
		track_vma(vma, i);
	    }
	    return i;
	}
    }        
    //not found
    status = track_vma(vma, new_vmas_size);
    if (status) printk(KERN_EMERG "Error Tracking VMA\n");
    new_vmas_size++;
    return new_vmas_size - 1;
}

static struct vma_desc *
get_vma_desc(struct vm_area_struct * vma)
{
    int i;
    i = index_of_vma(vma);
    return &(new_vmas[i]);
}

int
stop_scanner_thread(void)
{
    int status;
    hrtimer_cancel(&timer);
    status = kthread_stop(scanner_thread);
    if(status){
    	printk(KERN_ALERT "could not kill thread with return value:%d\n", status);
	return status;
    }

    return 0;
}

/*Do something, sleep. Rinse and repeat */
static int
scanner_func(void * args)
{
    while(1){
	count_vmas(my_mm);
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();
	if(kthread_should_stop()){
	    break;
	}
    }
    printk(KERN_ALERT "Exiting the loop, Terminating thread\n");
    return 0;
}

/* timer function */
static enum hrtimer_restart expiration_func(struct hrtimer * tim){
    wake_up_process(scanner_thread);
    hrtimer_forward_now(tim, kt);
    /* printk(KERN_ALERT "timer expired\n"); */
    return HRTIMER_RESTART;
}

int
launch_scanner_kthread(struct mm_struct * mm,
		       unsigned long log_sec,
		       unsigned long log_nsec)
{
    my_mm = mm;
    scanner_thread = kthread_run(scanner_func, NULL, "pg_sched_scanner");
    if (scanner_thread){
	kt = ktime_set(log_sec, log_nsec);
	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer.function = expiration_func;
	hrtimer_start(&timer, kt, HRTIMER_MODE_REL);
	return 0;
    }
    return -1;
}

void
register_init_vmas(struct mm_struct * mm)
{
    struct vm_area_struct *vma;

    down_write(&(mm->mmap_sem));
    for (vma = mm->mmap; vma != NULL; vma = vma->vm_next){
	/* This can never end well... */
	if (!vma_is_anonymous(vma)) continue; /*Only interested in Anon*/
	/* if (my_vma_is_stack_for_current(vma)) continue; /\*Don't count the stack*\/ */
	if (vma->vm_flags & VM_EXEC) continue;
	if (vma->vm_flags & VM_SPECIAL) continue; /* Dynamically Loaded Code Pages */

	init_vmas[init_vmas_size++] = vma;
    }
    up_write(&(mm->mmap_sem));
}

static int
is_new_vma(struct vm_area_struct *vma)
{
    int i;
    for (i = 0; i < init_vmas_size; ++i){
	if (vma == init_vmas[i]) return false;
    }
    return true;
}

struct pg_walk_data
{
    int non_usr_pages_4KB;
    int user_pages_4KB;
    int non_usr_pages_4MB;
    int user_pages_4MB;
    int n0;
    int n1;
    struct list_head * list;
    int list_size;
    struct vma_desc * vma_desc;
};

struct page* pg_sched_alloc(struct page *page, unsigned long private)
{
    /*Need to allocate free page on opposite NUMA node... */
    int target_node    = 0; /* Unhardcode later */
    gfp_t gfp_mask     = GFP_KERNEL; /*May need to set movable flag? */
    unsigned int order = 0;
    pg_data_t *pgdat;
    pgdat = page_pgdat(page);
    target_node = !pgdat->node_id;
    return alloc_pages_node(target_node, gfp_mask, order);
}

/* Function Symbols to look up */
fake_isolate_lru_page my_isolate_lru_page = NULL;
fake_vma_is_stack_for_current my_vma_is_stack_for_current = NULL;
fake_migrate_pages my_migrate_pages = NULL;

static int
pte_callback(pte_t *pte,
	     unsigned long addr,
	     unsigned long next,
	     struct mm_walk *walk)
{
    int status;
    struct page * page;
    pg_data_t *pgdat;
    unsigned long mask = _PAGE_USER | _PAGE_PRESENT;
    struct pg_walk_data * walk_data = (struct pg_walk_data *)walk->private;
    struct list_head * migration_list = walk_data->list;
    int pg_off;
    
    if ((pte_flags(*pte) & mask) != mask) return 0; /*NaBr0*/

    /*
      1) bump refcount for page (Don't think I need to do this)
      2) isolate_lru_page
      3) Check if the page is unevictable
      4) add to list by linkage on lru field?
      call migrate_pages -> This should do the rest?
     */

    /* Holy Shit */
    page = pte_page(*pte);
    /* printk(KERN_INFO "refcount on the page is %d\n", page_count(page)); */
    /* pgdat = page_pgdat(page); */
    /* if (pgdat->node_id == 0) */
    /* 	++walk_data->n0; */
    /* else */
    /* 	++walk_data->n1; */
    /* printk(KERN_INFO "NUMA NODE %d\n", pgdat->node_id); */
    /* { */
    /* 	int is_lru; */
    /* 	is_lru = PageLRU(page); */
    /* 	printk(KERN_INFO "is lru? : %d\n", is_lru); */
    /* } */

    if (pte_young(*pte)){
    	*pte = pte_mkold(*pte);//mkold
	pg_off = (addr - walk_data->vma_desc->vm_start) >> 12;
	walk_data->vma_desc->page_accesses[pg_off]++;
    }
    
    /*Page Access Stats*/

    /* if (PageLRU(page) && walk_data->list_size < 10 && */
    /* 	!PageUnevictable(page) && pgdat->node_id == 0){ */
    /* 	/\*Try to add page to list *\/ */
    /* 	/\* 2) *\/ */
    /* 	status = my_isolate_lru_page(page); */
    /* 	if (status){ */
    /* 	    printk(KERN_EMERG "ERROR ISOLATING\n"); */
    /* 	    return 0; */
    /* 	} */

    /* 	/\* 3) *\/ */
    /* 	if (PageUnevictable(page)){ */
    /* 	    printk(KERN_EMERG "PAGE IS UNEVICTABLE... PUT BACK!!\n"); */
    /* 	    return 0; */
    /* 	} */

    /* 	/\* 4) *\/ */
    /* 	list_add(&(page->lru), migration_list); */
    /* 	++walk_data->list_size; */
    /* 	printk(KERN_INFO "Page Added To Migration List\n"); */
    /* } */
    
    walk_data->user_pages_4KB++;

    return 0; /* MAYBE? */
}

static int
hugetlb_callback(pte_t *pte,
		 unsigned long hmask,
		 unsigned long addr,
		 unsigned long next,
		 struct mm_walk *walk)
{
    unsigned long mask = _PAGE_USER | _PAGE_PRESENT;
    struct pg_walk_data * walk_data = (struct pg_walk_data *)walk->private;
  
    if ((pte_flags(*pte) & mask) != mask) return 0; /*NaBr0*/

    walk_data->user_pages_4MB++;
  
    return 0; /* MAYBE? */
}


void
count_vmas(struct mm_struct * mm)
{
    struct vm_area_struct *vma;
    int status;
    LIST_HEAD(migration_list);
    
    struct pg_walk_data
	pg_walk_data =
	{
	    .non_usr_pages_4KB = 0,
	    .user_pages_4KB    = 0,
	    .non_usr_pages_4MB = 0,
	    .user_pages_4MB    = 0,
	    .n0                = 0,
	    .n1                = 0,
	    .list              = NULL,
	    .list_size         = 0,
	    .vma_desc          = NULL,
	};


    struct mm_walk_ops
	pg_sched_walk_ops =
	{
	    .pte_entry     = pte_callback,
	    .hugetlb_entry = hugetlb_callback,
	};
    
    down_write(&(mm->mmap_sem));
    for (vma = mm->mmap; vma != NULL; vma = vma->vm_next){
	/* This can never end well... */
	if (!vma_is_anonymous(vma)) continue; /*Only interested in Anon*/
	if (my_vma_is_stack_for_current(vma)) continue; /*Don't count the stack*/
	if (vma->vm_flags & VM_EXEC) continue;
	if (vma->vm_flags & VM_SPECIAL) continue; /* Dynamically Loaded Code Pages */
	if (!is_new_vma(vma)) continue;

	/* printk(KERN_EMERG "VMA ADDRESS: %lx\n", vma->vm_start); */
	pg_walk_data.list = &migration_list;
	pg_walk_data.vma_desc = get_vma_desc(vma);
	status = walk_page_vma(vma, &pg_sched_walk_ops, &pg_walk_data);
	if (status) printk(KERN_ALERT "PAGE WALK BAD\n");
    }
    up_write(&(mm->mmap_sem));

    /*Drain the migration list*/
    /*Figure out how to put stuff back on the LRU if we can't move it */
    /*Unless of course, migrate_pages does this already */

    /* status = my_migrate_pages(&migration_list, pg_sched_alloc, */
    /* 			   NULL, 0, MIGRATE_SYNC, MR_NUMA_MISPLACED); */

    /* if (status > 0) printk(KERN_EMERG "Couldnt move %d pages\n", status); */
    /* if (status < 0) printk(KERN_EMERG "Got a big boy error from migrate pages\n"); */
    
    printk(KERN_INFO "Found %d 4KB pages\n", pg_walk_data.user_pages_4KB);
    printk(KERN_INFO "node 0: %d ... node 1: %d\n", pg_walk_data.n0, pg_walk_data.n1);
}
