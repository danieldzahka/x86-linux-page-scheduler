#ifndef __PG_SCHED_MEM_H__
#define __PG_SCHED_MEM_H__

/* int pg_sched_scan_pgtbl(struct mm_struct *mm); */
void count_vmas(struct mm_struct * mm);
void register_init_vmas(struct mm_struct * mm);

int launch_scanner_kthread(struct mm_struct * mm,
		       unsigned long log_sec,
		       unsigned long log_nsec);

int stop_scanner_thread(void);

typedef int (*fake_isolate_lru_page)(struct page *page);
typedef int (*fake_vma_is_stack_for_current)(struct vm_area_struct *vma);

extern fake_isolate_lru_page my_isolate_lru_page;
extern fake_vma_is_stack_for_current my_vma_is_stack_for_current;

#endif /* __PG_SCHED_MEM_H__ */
