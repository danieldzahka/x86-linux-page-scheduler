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
#include <linux/migrate.h>
#include <linux/gfp.h>

#include <pg_sched_mem.h>

#define MAX_INIT_VMAS 20
#define MAX_NEW_VMAS 20

/* static int num_vmas; /\* May purposely become stale *\/ */

static int init_vmas_size;
static struct vm_area_struct * init_vmas[MAX_INIT_VMAS];

/* For Additional Anon VMAs */
static int new_vmas_size;
static struct vm_area_struct * new_vmas[MAX_NEW_VMAS];

static ktime_t kt;
static struct hrtimer timer;
static struct task_struct* scanner_thread = NULL;

static struct mm_struct* my_mm;

/* Return the idx of the region, store if not there */
static int
index_of_vma(struct vm_area_struct * vma)
{
    int i;
    for (i= 0; i < new_vmas_size; ++i){
	if (vma == new_vmas[i]) return i;
    }
    /*Not Found*/
    new_vmas[new_vmas_size++] = vma;
    BUG_ON(new_vmas_size >= MAX_NEW_VMAS);
    return new_vmas_size - 1;
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
    struct list_head * list;
    int list_size;
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
  
    if ((pte_flags(*pte) & mask) != mask) return 0; /*NaBr0*/
    /* if (pte_young(*pte)){ */
    /* 	*pte = pte_mkold(*pte);//mkold */
    /* 	printk(KERN_INFO "Found old page\n"); */
    /* } */

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
    /* printk(KERN_INFO "NUMA NODE %d\n", pgdat->node_id); */
    /* { */
    /* 	int is_lru; */
    /* 	is_lru = PageLRU(page); */
    /* 	printk(KERN_INFO "is lru? : %d\n", is_lru); */
    /* } */


    if (PageLRU(page) && walk_data->list_size < 10 && !PageUnevictable(page)){
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
	printk(KERN_INFO "Page Added To Migration List\n");
    }
    
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
	    .list              = NULL,
	    .list_size         = 0,
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

	printk(KERN_EMERG "VMA ADDRESS: %lx\n", vma->vm_start);
	pg_walk_data.list = &migration_list;
	status = walk_page_vma(vma, &pg_sched_walk_ops, &pg_walk_data);
	if (status) printk(KERN_ALERT "PAGE WALK BAD\n");
    }
    up_write(&(mm->mmap_sem));
    printk(KERN_INFO "Found %d 4KB pages\n", pg_walk_data.user_pages_4KB);
    /* printk(KERN_INFO "Found %d 4MB pages\n", pg_walk_data.user_pages_4MB); */
}



/* #include <linux/badger_trap.h> */

/* This is a modified version of the Page Table walk from */
/* badger trap https://research.cs.wisc.edu/multifacet/BadgerTrap/ */
/*
 * This function walks the page table of the process being marked for badger trap
 * This helps in finding all the PTEs that are to be marked as reserved. This is 
 * espicially useful to start badger trap on the fly using (2) and (3). If we do not
 * call this function, when starting badger trap for any process, we may miss some TLB 
 * misses from being tracked which may not be desierable.
 *
 * Note: This function takes care of transparent hugepages and hugepages in general.
 */

/* pte_t *huge_pte_offset(struct mm_struct *mm, */
/* 		       unsigned long addr, unsigned long sz); */

/*  int pg_sched_scan_pgtbl(struct mm_struct *mm) */
/* { */
/* 	pgd_t *pgd; */
/* 	pud_t *pud; */
/* 	pmd_t *pmd; */
/* 	pte_t *pte; */
/* 	pte_t *page_table; */
/* 	spinlock_t *ptl; */
/* 	unsigned long address; */
/* 	unsigned long i,j,k,l; */
/* 	unsigned long user = 0; */
/* 	unsigned long mask = _PAGE_USER | _PAGE_PRESENT; */
/* 	struct vm_area_struct *vma; */
/* 	pgd_t *base = mm->pgd; */
/* 	for(i=0; i<PTRS_PER_PGD; i++) */
/* 	{ */
/* 		pgd = base + i; */
/* 		if((pgd_flags(*pgd) & mask) != mask) */
/* 			continue; */
/* 		for(j=0; j<PTRS_PER_PUD; j++) */
/* 		{ */
/* 			pud = (pud_t *)pgd_page_vaddr(*pgd) + j; */
/* 			if((pud_flags(*pud) & mask) != mask) */
/*                         	continue; */
/* 			address = (i<<PGDIR_SHIFT) + (j<<PUD_SHIFT); */
/* 			if(vma && pud_huge(*pud) && is_vm_hugetlb_page(vma)) */
/* 			{ */
/* 				spin_lock(&mm->page_table_lock); */
/* 				/\* page_table = huge_pte_offset(mm, address , vma_mmu_pagesize(vma)); *\/ */
/* 				/\* *page_table = pte_mkreserve(*page_table); *\/ */
/* 				spin_unlock(&mm->page_table_lock); */
/* 				continue; */
/* 			} */
/* 			for(k=0; k<PTRS_PER_PMD; k++) */
/* 			{ */
/* 				pmd = (pmd_t *)pud_page_vaddr(*pud) + k; */
/* 				if((pmd_flags(*pmd) & mask) != mask) */
/* 					continue; */
/* 				address = (i<<PGDIR_SHIFT) + (j<<PUD_SHIFT) + (k<<PMD_SHIFT); */
/* 				vma = find_vma(mm, address); */
/* 				if(vma && pmd_huge(*pmd) && (transparent_hugepage_enabled(vma)||is_vm_hugetlb_page(vma))) */
/* 				{ */
/* 					spin_lock(&mm->page_table_lock); */
/* 					/\* *pmd = pmd_mkreserve(*pmd); *\/ */
/* 					spin_unlock(&mm->page_table_lock); */
/* 					continue; */
/* 				} */
/* 				for(l=0; l<PTRS_PER_PTE; l++) */
/* 				{ */
/* 					pte = (pte_t *)pmd_page_vaddr(*pmd) + l; */
/* 					if((pte_flags(*pte) & mask) != mask) */
/* 						continue; */
/* 					address = (i<<PGDIR_SHIFT) + (j<<PUD_SHIFT) + (k<<PMD_SHIFT) + (l<<PAGE_SHIFT); */
/* 					vma = find_vma(mm, address); */
/* 					if(vma) */
/* 					{ */
/* 						page_table = pte_offset_map_lock(mm, pmd, address, &ptl); */
/* 						/\* *pte = pte_mkreserve(*pte); *\/ */
/* 						pte_unmap_unlock(page_table, ptl); */
/* 					} */
/* 					user++; */
/* 				} */
/* 			} */
/* 		} */
/* 	} */

/* 	return user; */
/* } */
