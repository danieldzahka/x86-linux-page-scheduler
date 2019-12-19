#ifndef __PG_SCHED_MEM_H__
#define __PG_SCHED_MEM_H__
#include <linux/migrate.h>

/* int pg_sched_scan_pgtbl(struct mm_struct *mm); */
/* void count_vmas(struct mm_struct * mm); */
/* void register_init_vmas(struct mm_struct * mm); */

typedef int (*fake_isolate_lru_page)(struct page *page);
typedef int (*fake_vma_is_stack_for_current)(struct vm_area_struct *vma);
typedef int (*fake_migrate_pages)(struct list_head *l, new_page_t new, free_page_t free,
		unsigned long private, enum migrate_mode mode, int reason);

extern fake_isolate_lru_page my_isolate_lru_page;
extern fake_vma_is_stack_for_current my_vma_is_stack_for_current;
extern fake_migrate_pages my_migrate_pages;

#endif /* __PG_SCHED_MEM_H__ */
