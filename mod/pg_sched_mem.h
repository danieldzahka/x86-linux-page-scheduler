#ifndef __PG_SCHED_MEM_H__
#define __PG_SCHED_MEM_H__

/* int pg_sched_scan_pgtbl(struct mm_struct *mm); */
void count_vmas(struct mm_struct * mm);
void register_init_vmas(struct mm_struct * mm);

int launch_scanner_kthread(struct mm_struct * mm,
		       unsigned long log_sec,
		       unsigned long log_nsec);

int stop_scanner_thread(void);


#endif /* __PG_SCHED_MEM_H__ */
