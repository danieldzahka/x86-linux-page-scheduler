#ifndef __PG_SCHED_MEM_H__
#define __PG_SCHED_MEM_H__

/* int pg_sched_scan_pgtbl(struct mm_struct *mm); */
void count_vmas(struct mm_struct * mm);

#endif /* __PG_SCHED_MEM_H__ */
