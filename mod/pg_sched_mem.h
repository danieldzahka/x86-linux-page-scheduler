#ifndef __PG_SCHED_MEM_H__
#define __PG_SCHED_MEM_H__
#include <linux/migrate.h>
#include <linux/pagewalk.h>
#include <pg_sched_priv.h>

/* int pg_sched_scan_pgtbl(struct mm_struct *mm); */
/* void count_vmas(struct mm_struct * mm); */
/* void register_init_vmas(struct mm_struct * mm); */

void count_vmas(struct tracked_process * target_tracker);

typedef int (*fake_isolate_lru_page)(struct page *page);
typedef int (*fake_vma_is_stack_for_current)(struct vm_area_struct *vma);
typedef int (*fake_migrate_pages)(struct list_head *l, new_page_t new, free_page_t free,
		unsigned long private, enum migrate_mode mode, int reason);
typedef void (*fake_flush_tlb_mm_range)(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned int stride_shift,
				bool freed_tables);
typedef int (*fake_walk_page_vma)(struct vm_area_struct *vma, const struct mm_walk_ops *ops,
				  void *private);

extern fake_isolate_lru_page my_isolate_lru_page;
extern fake_vma_is_stack_for_current my_vma_is_stack_for_current;
extern fake_migrate_pages my_migrate_pages;
extern fake_flush_tlb_mm_range my_flush_tlb_mm_range;
extern fake_walk_page_vma my_walk_page_vma;
#endif /* __PG_SCHED_MEM_H__ */
