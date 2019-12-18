#ifndef __PG_SCHED_H__
#define __PG_SCHED_H__

#ifndef __x86_64
#error PG_SCHED only runs on x86_64 architectures
#endif

#ifdef __KERNEL__
#endif /* END __KERNEL__ */

#ifndef __KERNEL__

#define PAGE_SIZE_4KB               (1ULL << 12)
#define PAGE_SIZE_2MB               (1ULL << 21)
#define PAGE_SIZE_1GB               (1ULL << 30)

#ifndef PAGE_SIZE
#define PAGE_SIZE                   PAGE_SIZE_4KB
#endif

#define PAGE_MASK_PS(ps)            (~(ps - 1)) 
#define PAGE_ALIGN_DOWN(addr, ps)	(addr & PAGE_MASK_PS(ps))
#define PAGE_ALIGN_UP(addr, ps)		((addr + (ps - 1)) & PAGE_MASK_PS(ps))

#endif /* END USER */

/* BOTH USER AND KERNEL */
#define PG_SCHED_MODULE_NAME "pg_sched"
#define PG_SCHED_DEVICE_PATH "/dev/" PG_SCHED_MODULE_NAME

/* IOCTL ARGS*/
struct track_pid_arg {
    pid_t pid;
};

struct untrack_pid_arg {
    pid_t pid;
};

/* IOCTL */
#define PG_SCHED_MAGIC 0x9F
#define PG_SCHED_SCAN_PT     _IO(PG_SCHED_MAGIC, 1)
#define PG_SCHED_TRACK_PID   _IOW(PG_SCHED_MAGIC, 2, struct track_pid_arg * )
#define PG_SCHED_UNTRACK_PID _IOW(PG_SCHED_MAGIC, 3, struct untrack_pid_arg *)

#endif /* __PG_SCHED_H__ */
