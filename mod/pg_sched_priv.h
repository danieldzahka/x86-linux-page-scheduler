#ifndef __PG_SCHED_PRIV_H__
#define __PG_SCHED_PRIV_H__
//probably need to include some headers for stlye
//but putting below all includes should work nonetheless

struct initial_vma {
    struct vm_area_struct * vma;
    struct list_head linkage;
};

struct page_desc {
    int accesses;
    int last_touched;
    int node;
};

struct vma_desc {
    struct vm_area_struct * vma; /*Use as key*/
    unsigned long           vm_start;
    unsigned long           vm_end;
    struct page_desc *      page_accesses;
    int                     num_pages;

    struct list_head linkage; /* struct tracked_process -> vma_list */
};

struct tracked_process {
    pid_t pid;
    int migration_enabled;
    struct {
        int scans_to_be_idle;
        struct {
            unsigned long sec;
            unsigned long nsec;
        } period;
    } policy;

    struct mm_struct * mm;

    struct kref refcount;
    struct list_head linkage; /* List: (static global) pg_sched_tracked_pids */

    struct list_head vma_desc_list; /* VMA's that we are watching */
    int vma_desc_list_length; /* For debugging merged or disappearing vma's */

    struct list_head initial_vma_list; /*.so's and other stuff I dont want to touch*/
    
    struct scanner_params {
        ktime_t kt;
        struct hrtimer timer;
        struct task_struct* scanner_thread;
    } scanner_thread_struct;
    
    void (*release) (struct kref * refc);
};

int
get_vma_desc_add_if_absent(struct tracked_process * this,
                           struct vm_area_struct * vma,
                           struct vma_desc * res);

#endif /* __PG_SCHED_PRIV_H__ */
