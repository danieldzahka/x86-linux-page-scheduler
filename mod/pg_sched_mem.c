#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
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
#include <linux/random.h>
#include <linux/nodemask.h>

#include <pg_sched.h>
#include <pg_sched_mem.h>
#include <pg_sched_priv.h>

#define PRINT_IF_NULL(ptr,NUM) do {if (!(ptr)) printk(KERN_EMERG "FOUND IT! %d\n", NUM);} while(0);

/* static int */
/* is_new_vma(struct tracked_process * target_tracker, */
/* 	   struct vm_area_struct *vma) */
/* { */
/*     struct initial_vma * obj; */
    
/*     list_for_each_entry(obj, &target_tracker->initial_vma_list, linkage){ */
/* 	if (obj->vma == vma){ */
/* 	    return false; */
/* 	} */
/*     } */

/*     return true; */
/* } */

struct pg_walk_data
{
    int eligible1; /* Fast -> Slow */
    int eligible2; /* Slow -> Fast */
    int n0;
    int n1;
    int n2;
    struct list_head * list;  /* Fast -> Slow */
    int list_size;
    struct list_head * list2; /* Slow -> Fast */
    int list_size2;
    struct vma_desc * vma_desc;
    /* int migration_enabled; */
    /* int threshold; */
    /* int slow_pages; */
    struct tracked_process * tracked_pid;
    int priv_count1;
    int priv_count2;
};

/* Fast Node Pool [0,1], but I only use 1 for now*/
struct page* pg_sched_alloc_fast(struct page *page, unsigned long private)
{
    struct page * new  = NULL;
    int target_node    = 0; /* Unhardcode later */
    gfp_t gfp_mask     = GFP_KERNEL; /*CHANGE TO GFP_USER*/
    unsigned int order = 0;
    
    new = alloc_pages_node(target_node, gfp_mask, order);
    /* Patch page-hotness metadata */
    new->pg_word1 = 0; /* Reset random key */
    new->pg_word2 = 0; /* Reset page age*/

    return new;
}

/* Slow node pool [2], bc the memory is hotplugged you can't rely on these mappings */
struct page* pg_sched_alloc_slow(struct page *page, unsigned long private)
{
    struct page * new  = NULL;
    int target_node    = 2; /* Unhardcode later */
    gfp_t gfp_mask     = GFP_KERNEL; /* CHANGE TO GFP_USER */
    unsigned int order = 0;

    new = alloc_pages_node(target_node, gfp_mask, order);
    /* Patch page-hotness metadata */
    new->pg_word1 = page->pg_word1; /* copy random key */
    new->pg_word2 = page->pg_word2; /* copy page age*/

    return new;
}

/* Function Symbols to look up */
fake_isolate_lru_page my_isolate_lru_page = NULL;
fake_vma_is_stack_for_current my_vma_is_stack_for_current = NULL;
fake_migrate_pages my_migrate_pages = NULL;
fake_flush_tlb_mm_range my_flush_tlb_mm_range = NULL;
fake_walk_page_vma my_walk_page_vma = NULL;
fake_mpol_rebind_mm my_mpol_rebind_mm = NULL;
//consider looking up walk_vma here so ppl dont need to recompile the kernel

//These should get added to the tracker object...

/* static int threshold = 10; */
static int max_pages = 4000;
static unsigned int period = 0;

extern int * hist;

static int
update_page_metadata(struct page * page,
		     enum hotness_policy pol,
		     int accessed,
		     int node_id,
		     struct tracked_process * p,
		     struct pg_walk_data * w)
{
    int temp; 
    int should_migrate = 0; 
    
    int alpha = p->policy.alpha;
    int alpha_d = 1024; /* p->policy.alpha_d; */
    int theta   = p->policy.theta;

    /* int w1 = (int) ((~(1<<31)) & page->pg_word1); //force in range */
    int w2 = (int) ((~(1<<31)) & page->pg_word2); //force in range
    
    switch (pol){
    case AGE_THRESHOLD:
	/* w1 = 0; */
	w2 = accessed ? 0 : w2 + 1;
	should_migrate = ((node_id == 0 || node_id == 1) && w2 >= theta) || (node_id == 2 && w2 < theta);
	break;
    case EMA:
	/* w1 = 0; */
	/* w2 = w2 + alpha_n*(accessed*1024 - w2)/alpha_d; */
	w2 = w2 + mult_frac(accessed*4096 - w2, alpha, alpha_d);
	w2 = max(w2,0);
	should_migrate = ((node_id == 0 || node_id == 1) && w2 < theta) || (node_id == 2 && w2 >= theta);
	break;
    case HAMMING_WEIGHT:
	w2 = 0;
	should_migrate = ((node_id == 0 || node_id == 1) && w2 < theta) || (node_id == 2 && w2 >= theta);
	break;
    case NONE:
	break;
    default:
	break;
    }

    //do histogram dirty work...
    if (period == 400){
	if (w2 < hist_size) hist[w2]++; else hist[hist_size - 1]++;
    }

    w->priv_count1 = max(w->priv_count1, w2);
    
    /* page->pg_word1 = w1; */
    page->pg_word2 = (unsigned int) w2;

    return should_migrate;
}

/* TO DO: Inform tracker if VMA is largely unevictable... */
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
    struct list_head * migration_list = NULL;
    int * list_size;
    struct tracked_process * tracked_pid = walk_data->tracked_pid;
    int should_move;
    int alpha = tracked_pid->policy.alpha;
    int theta = tracked_pid->policy.theta;
    /* int threshold = tracked_pid->policy.scans_to_be_idle; */
    int migration_enabled = tracked_pid->migration_enabled;
    unsigned int key = tracked_pid->key;
    int * refd_page_count;
    int should_migrate = 0;
    int accessed = 0;
    enum hotness_policy pol = tracked_pid->policy.class;
    int node_id;
    
    if ((pte_flags(*pte) & mask) != mask) return 0; /*NaBr0*/

    /*
      1) bump refcount for page (Don't think I need to do this)
      2) isolate_lru_page
      3) Check if the page is unevictable
      4) add to list by linkage on lru field?
      call migrate_pages -> This should do the rest?
     */
    
    page = pte_page(*pte);
    pgdat = page_pgdat(page);
    
    migration_list = walk_data->list; /* Fast -> Slow */
    list_size      = &walk_data->list_size;
    refd_page_count= &walk_data->priv_count1;
    node_id = pgdat->node_id;
    if (node_id == 0){
    	++walk_data->n0;
    }
    else if (node_id == 1){
    	++walk_data->n1;
    }
    else{
	++walk_data->n2;
	migration_list = walk_data->list2; /* Slow -> Fast*/
	list_size      = &walk_data->list_size2;
	refd_page_count= &walk_data->priv_count2;
    }

    /* I don't think it's legal to deref a struct page * without
     * holding some kind of page lock... oh well */
    /* Also may present some issues if 2 processes are both tracked and share pages,
     * But that shouldnt be unsolvable */
    if (key != page->pg_word1){
    	page->pg_word1 = key;
    	page->pg_word2 = 0;
    }
    
    if (pte_young(*pte)){
    	*pte = pte_mkold(*pte);
	accessed = 1;
    }
    
    should_migrate = update_page_metadata(page, pol, accessed, node_id, tracked_pid,walk_data);
    
    /* if (pgdat->node_id == 2 && period > 40){ */
    /* 	int r = get_random_int(); */
    /* 	r &= ~(1 << 31); */
    /* 	if (r % walk_data->slow_pages <= max_pages){ */
    /* 	    should_move = 1; */
    /* 	} */
    /* } */
    
    if (migration_enabled && PageLRU(page) && *list_size < max_pages &&
    	!PageUnevictable(page) && should_move /* && period > 400 */){
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
    	++(*list_size);
    	/* printk(KERN_INFO "Page Added To Migration List\n"); */
    }

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
    /* struct pg_walk_data * walk_data = (struct pg_walk_data *)walk->private; */
  
    if ((pte_flags(*pte) & mask) != mask) return 0; /*NaBr0*/
  
    return 0; /* MAYBE? */
}


/* this function name has become quite a misnomer...*/
void
count_vmas(struct tracked_process * target_tracker)
{
    struct mm_struct * mm;
    struct vm_area_struct *vma;
    int status;
    LIST_HEAD(migration_list);
    LIST_HEAD(migration_list2);
    
    struct pg_walk_data
	pg_walk_data =
	{
	    .eligible1          = 0,
	    .eligible2          = 0,
	    .n0                 = 0,
	    .n1                 = 0,
	    .list               = &migration_list, /* Fast -> Slow */
	    .list_size          = 0,
	    .list2              = &migration_list2, /* Slow -> Fast */
	    .list_size2         = 0,
	    .vma_desc           = NULL,
	    /* .migration_enabled = target_tracker->migration_enabled, */
	    /* .threshold         = target_tracker->policy.scans_to_be_idle, */
	    /* .slow_pages        = target_tracker->slow_pages == 0 ? 1 : target_tracker->slow_pages, */
	    .tracked_pid        = target_tracker,
	    .priv_count1        = 0,
	    .priv_count2        = 0,
	};

    struct mm_walk_ops
	pg_sched_walk_ops =
	{
	    .pte_entry     = pte_callback,
	    .hugetlb_entry = hugetlb_callback,
	};

    mm = target_tracker->mm;

    ++period;
    
    down_write(&(mm->mmap_sem));
    for (vma = mm->mmap; vma != NULL; vma = vma->vm_next){
	/* if (!vma_is_anonymous(vma)) continue; /\*Only interested in Anon*\/ */
	if (vma->vm_flags & (VM_EXEC | VM_PFNMAP | VM_SPECIAL | VM_SHARED)) continue;

	status = get_vma_desc_add_if_absent(target_tracker, vma,
				   &pg_walk_data.vma_desc);

	if (status){
	    printk(KERN_EMERG "Error: get_vma_desc_add_if_absent failed!\n");
	    continue;
	}
	pg_walk_data.vma_desc->touched = 1;
	
	status = my_walk_page_vma(vma, &pg_sched_walk_ops, &pg_walk_data);
	if (status) printk(KERN_ALERT "PAGE WALK BAD\n");
    }
    up_write(&(mm->mmap_sem));

    /* Flush just to see how bad it gets */
    /* my_flush_tlb_mm_range(mm, 0UL, TLB_FLUSH_ALL, 0UL, true); */

    /*Drain the migration list*/
    /*Figure out how to put stuff back on the LRU if we can't move it */
    /*Unless of course, migrate_pages does this already */

    if (target_tracker->migration_enabled){
	if (pg_walk_data.list_size){
	    /* printk(KERN_EMERG "Attempting to migrate page\n"); */
	    status = my_migrate_pages(&migration_list, pg_sched_alloc_slow,
				      NULL, 0, MIGRATE_SYNC, MR_NUMA_MISPLACED);

	    /* Need to insert call to putback_lru_pages here...*/
	    /* From kernel source: 
	     * The caller should call putback_movable_pages() to return pages to the LRU
	     * or free list only if ret != 0.
	     */

        
	    if (status != 0) {
		printk(KERN_EMERG "Couldnt move %d pages... putting back on lru\n", status);
		//putback_movable_pages(&migration_list);
	    }
	    /* if (status < 0) printk(KERN_EMERG "Got a big boy error from migrate pages\n");	 */
	}
	if (pg_walk_data.list_size2){
	    /* printk(KERN_EMERG "Attempting to migrate page\n"); */
	    status = my_migrate_pages(&migration_list2, pg_sched_alloc_fast,
				      NULL, 0, MIGRATE_SYNC, MR_NUMA_MISPLACED);

	    /* Need to insert call to putback_lru_pages here...*/
	    /* From kernel source: 
	     * The caller should call putback_movable_pages() to return pages to the LRU
	     * or free list only if ret != 0.
	     */

        
	    if (status != 0) {
		printk(KERN_EMERG "Couldnt move %d pages... putting back on lru\n", status);
		//putback_movable_pages(&migration_list);
	    }
	    /* if (status < 0) printk(KERN_EMERG "Got a big boy error from migrate pages\n");	 */
	}
    }

    /* Free Stale VMA_DESC */
    free_unctouched_vmas(target_tracker);

    target_tracker->slow_pages = pg_walk_data.n2;
    
    /* printk(KERN_ALERT "B\n"); */
    /* printk(KERN_INFO "Found %d 4KB pages\n", pg_walk_data.user_pages_4KB); */
    printk(KERN_INFO "node 0: %d, node 1: %d, node 2: %d, migrations (Fast -> Slow): %d / %d, migrations (Slow -> Fast): %d / %d, Ref'd pages n0,n2: %d, %d\n",
	   pg_walk_data.n0, pg_walk_data.n1, pg_walk_data.n2, pg_walk_data.list_size, pg_walk_data.eligible1, pg_walk_data.list_size2, pg_walk_data.eligible2,
	   pg_walk_data.priv_count1, pg_walk_data.priv_count2);
    
    /* #if PG_SCHED_FIRST_TOUCH */
    /* { */
    /* static int a; */
    /* a = 1; */
    /* if (a && pg_walk_data.n0 > 10000){ */
    /* 	nodemask_t mask; */
    /* 	mask = nodemask_of_node(2); */
    /* 	my_mpol_rebind_mm(mm, &mask); */
    /* 	target_tracker->policy.period.sec  = 1; /\* Change period *\/ */
    /* 	target_tracker->policy.period.nsec = 0; */
    /* 	a = 0; */
    /* 	printk(KERN_INFO "CHANGING\n"); */
    /* } */
    /* } */
    /* #endif */
}
