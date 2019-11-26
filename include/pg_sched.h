#ifndef __PG_SCHED_H__
#define __PG_SCHED_H__

#ifndef __x86_64
#error PG_SCHED only runs on x86_64 architectures
#endif

#ifdef __KERNEL__
#endif /* END __KERNEL__ */

#ifndef __KERNEL__
#endif /* END USER */

/* BOTH USER AND KERNEL */
#define PG_SCHED_MODULE_NAME "pg_sched"
#define PG_SCHED_DEVICE_PATH "/dev/" PG_SCHED_MODULE_NAME

#endif /* __PG_SCHED_H__ */
