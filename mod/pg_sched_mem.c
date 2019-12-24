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
#include <pg_sched_priv.h>

static int
is_new_vma(struct tracked_process * target_tracker,
	   struct vm_area_struct *vma)
{
    struct initial_vma * obj;
    
    list_for_each_entry(obj, &target_tracker->initial_vma_list, linkage){
	if (obj->vma == vma){
	    return false;
	}
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
//consider looking up walk_vma here so ppl dont need to recompile the kernel

//These should get added to the tracker object...
static int current_period = 0;
static int threshold = 10;
static int max_pages = 1000;

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
    int should_move;
    
    if ((pte_flags(*pte) & mask) != mask) return 0; /*NaBr0*/

    #if 0
    
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
    pgdat = page_pgdat(page);
    if (pgdat->node_id == 0)
    	++walk_data->n0;
    else
    	++walk_data->n1;
    /* printk(KERN_INFO "NUMA NODE %d\n", pgdat->node_id); */
    /* { */
    /* 	int is_lru; */
    /* 	is_lru = PageLRU(page); */
    /* 	printk(KERN_INFO "is lru? : %d\n", is_lru); */
    /* } */
    should_move = 0;
    pg_off = (addr - walk_data->vma_desc->vm_start) >> 12;
    walk_data->vma_desc->page_accesses[pg_off].node = pgdat->node_id;
    if (pte_young(*pte)){
    	*pte = pte_mkold(*pte);//mkold
	walk_data->vma_desc->page_accesses[pg_off].last_touched = current_period;
	walk_data->vma_desc->page_accesses[pg_off].accesses++;
	if (pgdat->node_id == 1){
	    //should_move = 1; //fault back in
	}
    } else {
	if (current_period - walk_data->vma_desc->page_accesses[pg_off].last_touched > threshold){
	    should_move = 1;
	}
    }
    
    if (PageLRU(page) && walk_data->list_size < max_pages &&
    	!PageUnevictable(page) && should_move && pgdat->node_id == 0){
    	/*Try to add page to list */
    	/* 2) */
    	status = my_isolate_lru_page(page);
    	if (status){
    	    printk(KERN_EMERG "ERROR ISOLATING\n");
    	    return 0;
    	}

    	/* 3) */
    	if (PageUnevictable(page)){
    	    printk(KERN_EMERG "PAGE IS UNEVICTABLE... PUT BACK!!\n");
    	    return 0;
    	}

    	/* 4) */
    	list_add(&(page->lru), migration_list);
    	++walk_data->list_size;
    	/* printk(KERN_INFO "Page Added To Migration List\n"); */
    }

    #endif
    
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
count_vmas(struct tracked_process * target_tracker)
{
    struct mm_struct * mm;
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
	    .list              = &migration_list,
	    .list_size         = 0,
	    .vma_desc          = NULL,
	};


    struct mm_walk_ops
	pg_sched_walk_ops =
	{
	    .pte_entry     = pte_callback,
	    .hugetlb_entry = hugetlb_callback,
	};

    mm = target_tracker->mm;
    current_period++;
    
    down_write(&(mm->mmap_sem));
    for (vma = mm->mmap; vma != NULL; vma = vma->vm_next){
	/* printk(KERN_EMERG "VMA ADDRESS: %lx - %lx\n", vma->vm_start, vma->vm_end); */
	/* This can never end well... */
	/* if (!vma_is_anonymous(vma)) continue; /\*Only interested in Anon*\/ */
	/* if (my_vma_is_stack_for_current(vma)) continue; /\*Don't count the stack*\/ */
	if ((vma->vm_flags & VM_EXEC)    ||
	    (vma->vm_flags & VM_PFNMAP)  ||
	    (vma->vm_flags & VM_SPECIAL) ||
	    (!is_new_vma(target_tracker, vma))) continue;

	status = get_vma_desc_add_if_absent(target_tracker, vma,
				   pg_walk_data.vma_desc);

	if (status){
	    printk(KERN_EMERG "Error: get_vma_desc_add_if_absent failed!\n");
	    continue;
	}
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
    /* printk(KERN_INFO "node 0: %d ... node 1: %d\n", pg_walk_data.n0, pg_walk_data.n1); */
}
