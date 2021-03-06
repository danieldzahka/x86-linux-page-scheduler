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

struct pg_walk_data
{
    int eligible1; /* Fast -> Slow */
    int eligible2; /* Slow -> Fast */
    int n0;
    int n1;
    int n2;
    struct list_head ** demotion_vector;  /* Fast -> Slow */
    int list_size;
    struct list_head ** promotion_vector; /* Slow -> Fast */
    int list_size2;
    struct vma_desc * vma_desc;
    /* int migration_enabled; */
    /* int threshold; */
    /* int slow_pages; */
    struct tracked_process * tracked_pid;
    int priv_count1;
    int priv_count2;
    int priv_count3;
    int hot;
    int huge;
};

//These two functions need to be refactored to one
/* Fast Node Pool [0,1], but I only use 1 for now*/
struct page* pg_sched_alloc(struct page *page, unsigned long private)
{
    struct page * new  = NULL;
    int target_node    = (int) private;
    gfp_t gfp_mask     = GFP_USER;
    unsigned int order = 0;
    
    new = alloc_pages_node(target_node, gfp_mask, order);

    new->pg_word1 = page->pg_word1; /* copy random key */
    new->pg_word2 = page->pg_word2; /* copy page age*/

    return new;
}

/* struct page* pg_sched_alloc_huge(struct page *page, unsigned long private) */
/* { */
/*     struct page * new  = NULL; */
/*     int target_node    = (int) private; */
/*     gfp_t gfp_mask     = GFP_USER | __GFP_COMP; */
/*     unsigned int order = 9; */
    
/*     new = alloc_pages_node(target_node, gfp_mask, order); */

/*     new->pg_word1 = page->pg_word1; /\* copy random key *\/ */
/*     new->pg_word2 = page->pg_word2; /\* copy page age*\/ */

/*     return new; */
/* } */


/* Slow node pool [2], bc the memory is hotplugged you can't rely on these mappings */
/* struct page* pg_sched_alloc_slow(struct page *page, unsigned long private) */
/* { */
/*     struct page * new  = NULL; */
/*     int target_node    = 2; /\* Unhardcode later *\/ */
/*     gfp_t gfp_mask     = GFP_USER; /\* CHANGE TO GFP_USER *\/ */
/*     unsigned int order = 0; */

/*     new = alloc_pages_node(target_node, gfp_mask, order); */
/*     /\* Patch page-hotness metadata *\/ */
/*     new->pg_word1 = page->pg_word1; /\* copy random key *\/ */
/*     new->pg_word2 = page->pg_word2; /\* copy page age*\/ */

/*     return new; */
/* } */

/* Function Symbols to look up */
fake_isolate_lru_page my_isolate_lru_page = NULL;
fake_vma_is_stack_for_current my_vma_is_stack_for_current = NULL;
fake_migrate_pages my_migrate_pages = NULL;
fake_flush_tlb_mm_range my_flush_tlb_mm_range = NULL;
fake_walk_page_vma my_walk_page_vma = NULL;
fake_mpol_rebind_mm my_mpol_rebind_mm = NULL;
fake_isolate_huge_page my_isolate_huge_page = NULL;
//consider looking up walk_vma here so ppl dont need to recompile the kernel

//These should get added to the tracker object...

/* static int threshold = 10; */
/* static unsigned int period = 0; */

extern int * hist;

static int
update_page_metadata(struct page * page,
		     enum hotness_policy pol,
		     int accessed,
		     int node_id,
		     struct tracked_process * p,
		     struct pg_walk_data * w)
{
    /* int temp = 0; */
    int should_migrate = 0; 
    
    int alpha = p->policy.alpha;
    int alpha_d = 1024; /* p->policy.alpha_d; */
    int theta   = p->policy.theta;
    int tolerance = 0;

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
	/* temp = mult_frac(accessed*4096 - w2, alpha, alpha_d); */
	w2 = w2 + mult_frac(accessed*4096 - w2, alpha, alpha_d);
	w2 = max(w2,0);
	/* w2 = w2 + accessed; */
	should_migrate = ((node_id == 0) && w2 < theta - tolerance) || ((node_id == 2 || node_id == 1) && w2 >= theta + tolerance);
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
    if (p->policy.epoch == 140){
    	if (w2 < hist_size) hist[w2]++; else hist[hist_size - 1]++;
    }
    if (w2 > theta) w->hot++;    
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
    int migration_enabled = tracked_pid->migration_enabled && (tracked_pid->policy.epoch % tracked_pid->policy.mig_cycle == 0);
    unsigned int key = tracked_pid->key;
    int * refd_page_count;
    int * eligible;
    int should_migrate = 0;
    int accessed = 0;
    enum hotness_policy pol = tracked_pid->policy.class;
    int node_id;
    int max_pages;

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
    
    migration_list = walk_data->demotion_vector[min(9, walk_data->list_size / 4000)]; /* Fast -> Slow */
    list_size      = &walk_data->list_size;
    refd_page_count= &walk_data->priv_count1;
    eligible       = &walk_data->eligible1;
    max_pages      = tracked_pid->policy.max_demote;
    node_id = pgdat->node_id;
    if (node_id == 0){
    	++walk_data->n0;
    }
    else if (node_id == 1){
    	++walk_data->n1;
	refd_page_count= &walk_data->priv_count3;
	/* migration_list = walk_data->promotion_vector[min(9, walk_data->list_size2 / 4000)]; /\* Slow -> Fast*\/ */
	/* list_size      = &walk_data->list_size2; */
	/* refd_page_count= &walk_data->priv_count3; */
	/* eligible       = &walk_data->eligible2; */
	/* max_pages      = tracked_pid->policy.max_promote; */
    }
    else{
	++walk_data->n2;
	migration_list = walk_data->promotion_vector[min(9, walk_data->list_size2 / 4000)]; /* Slow -> Fast*/
	list_size      = &walk_data->list_size2;
	refd_page_count= &walk_data->priv_count2;
	eligible       = &walk_data->eligible2;
	max_pages      = tracked_pid->policy.max_promote;
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
	++(*refd_page_count);
    }
    
    should_migrate = update_page_metadata(page, pol, accessed, node_id, tracked_pid,walk_data);

    if (should_migrate) (*eligible)++;
    
    if (migration_enabled && PageLRU(page) && *list_size < max_pages &&
    	!PageUnevictable(page) && should_migrate && tracked_pid->policy.epoch > tracked_pid->policy.warmup_scans){
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
    /* int status; */
    /* struct page * page; */
    /* pg_data_t *pgdat; */
    unsigned long mask = _PAGE_USER | _PAGE_PRESENT;
    /* struct pg_walk_data * walk_data = (struct pg_walk_data *)walk->private; */
    /* struct list_head * migration_list = NULL; */
    /* int * list_size; */
    /* struct tracked_process * tracked_pid = walk_data->tracked_pid; */
    /* int migration_enabled = tracked_pid->migration_enabled && (tracked_pid->policy.epoch % tracked_pid->policy.mig_cycle == 0); */
    /* unsigned int key = tracked_pid->key; */
    /* int * refd_page_count; */
    /* int * eligible; */
    /* int should_migrate = 0; */
    /* int accessed = 0; */
    /* enum hotness_policy pol = tracked_pid->policy.class; */
    /* int node_id; */
    /* int max_pages; */

    if ((pte_flags(*pte) & mask) != mask) return 0; /*NaBr0*/

    return 0;
    
    /* page = pte_page(*pte); */
    /* pgdat = page_pgdat(page); */
    
    /* migration_list = walk_data->demotion_vector[min(9, walk_data->list_size / 4000)]; /\* Fast -> Slow *\/ */
    /* list_size      = &walk_data->list_size; */
    /* refd_page_count= &walk_data->priv_count1; */
    /* eligible       = &walk_data->eligible1; */
    /* max_pages      = tracked_pid->policy.max_demote; */
    /* node_id = pgdat->node_id; */
    /* if (node_id == 0){ */
    /* 	++walk_data->n0; */
    /* } */
    /* else if (node_id == 1){ */
    /* 	++walk_data->n1; */
    /* 	refd_page_count= &walk_data->priv_count3; */
    /* 	/\* migration_list = walk_data->promotion_vector[min(9, walk_data->list_size2 / 4000)]; /\\* Slow -> Fast*\\/ *\/ */
    /* 	/\* list_size      = &walk_data->list_size2; *\/ */
    /* 	/\* refd_page_count= &walk_data->priv_count3; *\/ */
    /* 	/\* eligible       = &walk_data->eligible2; *\/ */
    /* 	/\* max_pages      = tracked_pid->policy.max_promote; *\/ */
    /* } */
    /* else{ */
    /* 	++walk_data->n2; */
    /* 	migration_list = walk_data->promotion_vector[min(9, walk_data->list_size2 / 4000)]; /\* Slow -> Fast*\/ */
    /* 	list_size      = &walk_data->list_size2; */
    /* 	refd_page_count= &walk_data->priv_count2; */
    /* 	eligible       = &walk_data->eligible2; */
    /* 	max_pages      = tracked_pid->policy.max_promote; */
    /* } */

    /* if (key != page->pg_word1){ */
    /* 	page->pg_word1 = key; */
    /* 	page->pg_word2 = 0; */
    /* } */
    
    /* if (pte_young(*pte)){ */
    /* 	*pte = pte_mkold(*pte); */
    /* 	accessed = 1; */
    /* 	++(*refd_page_count); */
    /* } */

    /* should_migrate = update_page_metadata(page, pol, accessed, node_id, tracked_pid,walk_data); */

    /* if (should_migrate) (*eligible)++; */

    /*     if (migration_enabled && *list_size < max_pages && */
    /* 	!PageUnevictable(page) && should_migrate && tracked_pid->policy.epoch > 10){ */
    /* 	/\*Try to add page to list *\/ */
    /* 	/\* 2) *\/ */
    /* 	status = my_isolate_huge_page(page, migration_list); */
    /* 	if (!status){ */
    /* 	    printk(KERN_EMERG "ERROR ISOLATING\n"); */
    /* 	    return 0; */
    /* 	} */
    /* 	++(*list_size); */

    /* 	/\* 3) *\/ */
    /* 	if (PageUnevictable(page)){ */
    /* 	    printk(KERN_EMERG "PAGE IS UNEVICTABLE... PUT BACK!!\n"); */
    /* 	    return 0; */
    /* 	} */

    /* 	/\* 4) *\/ */
    /* 	/\* list_add(&(page->lru), migration_list); *\/ */
    /* } */
    
    /* return 0; /\* MAYBE? *\/ */
}

//make this take an alloc function pointer
void
move_page_vector(struct list_head * vector [],
		 int n,
		 int target)
{
    int status;
    struct list_head * l = NULL;
    int i = 0;
    int j = n / 4000;
    if (n % 4000 == 0) --j; /* 4k -> 0, 8k -> 1 ... 40k -> 9 */
    for (i = 0; i <= j; ++i){
	l = vector[i];
	status = my_migrate_pages(l, pg_sched_alloc, NULL, (unsigned long)target,
				  MIGRATE_SYNC, MR_NUMA_MISPLACED);
        
	if (status != 0) {
	    printk(KERN_EMERG "Couldnt move %d pages... putting back on lru\n", status);
	    //putback_movable_pages(&migration_list);
	}	
    }
}

#define PG_PROMOTION_VECSIZE 10
#define PG_DEMOTION_VECSIZE 1

/* this function name has become quite a misnomer...*/
void
count_vmas(struct tracked_process * target_tracker)
{
    struct mm_struct * mm;
    struct vm_area_struct *vma;
    int status;
    struct list_head * promotion_vector [PG_PROMOTION_VECSIZE];
    struct list_head * demotion_vector  [PG_PROMOTION_VECSIZE];

    /* Promotion Nodes */
    LIST_HEAD(m_1);
    LIST_HEAD(m_2);
    LIST_HEAD(m_3);
    LIST_HEAD(m_4);
    LIST_HEAD(m_5);
    LIST_HEAD(m_6);
    LIST_HEAD(m_7);
    LIST_HEAD(m_8);
    LIST_HEAD(m_9);
    LIST_HEAD(m_10);

    /* Demotion Nodes */
    LIST_HEAD(d_1);
    LIST_HEAD(d_2);
    LIST_HEAD(d_3);
    LIST_HEAD(d_4);
    LIST_HEAD(d_5);
    LIST_HEAD(d_6);
    LIST_HEAD(d_7);
    LIST_HEAD(d_8);
    LIST_HEAD(d_9);
    LIST_HEAD(d_10);
   
    struct pg_walk_data
	pg_walk_data =
	{
	 .eligible1          = 0,
	 .eligible2          = 0,
	 .n0                 = 0,
	 .n1                 = 0,
	 .demotion_vector    = demotion_vector,  /* Fast -> Slow */
	 .list_size          = 0,
	 .promotion_vector   = promotion_vector, /* Slow -> Fast */
	 .list_size2         = 0,
	 .vma_desc           = NULL,
	 .tracked_pid        = target_tracker,
	 .priv_count1        = 0,
	 .priv_count2        = 0,
	 .priv_count3        = 0,
	 .hot                = 0,
	 .huge               = 0,
	};
        
    struct mm_walk_ops
	pg_sched_walk_ops =
	{
	    .pte_entry     = pte_callback,
	    .hugetlb_entry = hugetlb_callback,
	};

    promotion_vector[0]  = &m_1;
    promotion_vector[1]  = &m_2;    
    promotion_vector[2]  = &m_3;
    promotion_vector[3]  = &m_4;    
    promotion_vector[4]  = &m_5;
    promotion_vector[5]  = &m_6;    
    promotion_vector[6]  = &m_7;
    promotion_vector[7]  = &m_8;    
    promotion_vector[8]  = &m_9;
    promotion_vector[9]  = &m_10;    

    demotion_vector[0]  = &d_1;
    demotion_vector[1]  = &d_2;    
    demotion_vector[2]  = &d_3;
    demotion_vector[3]  = &d_4;    
    demotion_vector[4]  = &d_5;
    demotion_vector[5]  = &d_6;    
    demotion_vector[6]  = &d_7;
    demotion_vector[7]  = &d_8;    
    demotion_vector[8]  = &d_9;
    demotion_vector[9]  = &d_10;    
    
    mm = target_tracker->mm;

    /* printk(KERN_INFO "A\n"); */
    
    down_write(&(mm->mmap_sem));
    for (vma = mm->mmap; vma != NULL; vma = vma->vm_next){
	if (vma->vm_flags & (VM_EXEC | VM_PFNMAP | VM_SPECIAL | VM_SHARED)) continue;
	/* if (!(vma->vm_flags & VM_SPECIAL)) continue; */
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

    /* printk(KERN_INFO "B\n"); */
    /* Flush just to see how bad it gets */
    /* my_flush_tlb_mm_range(mm, 0UL, TLB_FLUSH_ALL, 0UL, true); */

    /*Drain the migration list*/
    /*Figure out how to put stuff back on the LRU if we can't move it */
    /*Unless of course, migrate_pages does this already */

    if (target_tracker->migration_enabled){
	if (pg_walk_data.list_size){
	    /* printk(KERN_EMERG "Attempting to migrate page\n"); */
	    /* status = my_migrate_pages(&migration_list, pg_sched_alloc_slow, */
	    /* 			      NULL, 0, MIGRATE_SYNC, MR_NUMA_MISPLACED); */

	    move_page_vector(demotion_vector, pg_walk_data.list_size, 2);
	    
	    /* Need to insert call to putback_lru_pages here...*/
	    /* From kernel source: 
	     * The caller should call putback_movable_pages() to return pages to the LRU
	     * or free list only if ret != 0.
	     */

	    /* if (status < 0) printk(KERN_EMERG "Got a big boy error from migrate pages\n");	 */
	}
	if (pg_walk_data.list_size2){
	    /* printk(KERN_EMERG "Attempting to migrate page\n"); */
	    /* status = my_migrate_pages(&migration_list2, pg_sched_alloc_fast, */
	    /* 			      NULL, 0, MIGRATE_SYNC, MR_NUMA_MISPLACED); */

	    /* Need to insert call to putback_lru_pages here...*/
	    /* From kernel source: 
	     * The caller should call putback_movable_pages() to return pages to the LRU
	     * or free list only if ret != 0.
	     */

	    move_page_vector(promotion_vector, pg_walk_data.list_size2, 0);
	    /* if (status != 0) { */
	    /* 	printk(KERN_EMERG "Couldnt move %d pages... putting back on lru\n", status); */
	    /* 	//putback_movable_pages(&migration_list); */
	    /* } */
	    /* if (status < 0) printk(KERN_EMERG "Got a big boy error from migrate pages\n");	 */
	}
    }

    /* Free Stale VMA_DESC */
    free_unctouched_vmas(target_tracker);
    printk(KERN_INFO "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
	   pg_walk_data.n0, pg_walk_data.n1, pg_walk_data.n2, pg_walk_data.list_size, pg_walk_data.eligible1,
	   pg_walk_data.list_size2, pg_walk_data.eligible2, pg_walk_data.priv_count1, pg_walk_data.priv_count3,pg_walk_data.priv_count2,
           target_tracker->policy.theta, target_tracker->policy.epoch);        
    
    /* Do our control logic here...*/
    {
	int dram_low;
	int dram_high;
	int dram_targ;
	int denom = 20;
	int err   = 1;
	int total = pg_walk_data.n0 + pg_walk_data.n1 + pg_walk_data.n2;
	int actual= pg_walk_data.n0 + pg_walk_data.list_size2 - pg_walk_data.list_size;

	dram_low  = mult_frac(total, target_tracker->policy.ratio - err , denom);
	dram_high = mult_frac(total, target_tracker->policy.ratio + err , denom);
	dram_targ = mult_frac(total, target_tracker->policy.ratio, denom);

	//use randomness to get selection of eligible pages (only for promotion though...)
	//during scan keep track of % pages that could be hot, adjust cutoff so that you at least have
	//enough to fill
	if (actual < dram_low){
	    //cap demotion
	    target_tracker->policy.max_demote = 0;
	    target_tracker->policy.max_promote = target_tracker->policy.max_migrations;
	} else if (actual > dram_high){
	    //cap promotion
	    target_tracker->policy.max_demote = target_tracker->policy.max_migrations;
	    target_tracker->policy.max_promote = 0;
	} else {
	    //in the target zone
	    /* if (target_tracker->policy.epoch > 20) */
	    /*     target_tracker->policy.mig_cycle = 1; */
	}

	//control thresholds
	//really just make sure that we have enough pages eligible for DRAM
	if (target_tracker->policy.epoch > target_tracker->policy.warmup_scans){
	    if (pg_walk_data.hot < dram_targ){
		//need to lower the cutoff
		target_tracker->policy.theta = max(mult_frac(target_tracker->policy.theta, 4, 5), 1);
	    } else if (pg_walk_data.hot > dram_high){
		//raise the bar... this really may not be necessary
		target_tracker->policy.theta = min(target_tracker->policy.theta+100, 3500);
	    }
	}
	
    }
    target_tracker->policy.epoch++;
    
    /* printk(KERN_ALERT "B\n"); */
}
