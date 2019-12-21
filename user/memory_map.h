/*
 * This file is part of the invirt project developed at Washington
 * University in St. Louis
 *
 * This is free software.  You are permitted to use, redistribute, and
 * modify it as specified in the file "LICENSE.md".
 */

#ifndef __MEMORY_MAP_H__
#define __MEMORY_MAP_H__

#include <stdint.h>
#include <stdbool.h>

/* #include <invirt.h> */
#include <pg_sched.h>

/* 
 * invirt flags 
 */
#define INV_FLAG_HEAP       0x100
#define INV_FLAG_STACK      0x200
#define INV_FLAG_VVAR       0x400   /* vvar/vdso need special treatment */
#define INV_FLAG_VDSO       0x800   /* vvar/vdso need special treatment */
#define INV_FLAG_VSYSCALL   0x1000  /* vsys does not need mapped */
#define INV_FLAG_INVIRT     0x2000  /* segment mapped in via invirt */

#define INV_FLAG_READ       0x10000
#define INV_FLAG_WRITE      0x20000
#define INV_FLAG_EXEC       0x40000

#define MAX_SEGMENTS        128

struct memory_segment {
    unsigned long start;
    unsigned long end;
    unsigned long pt_flags;
    unsigned long flags;
};

struct memory_map {
    pid_t pid;
    struct memory_segment segments[MAX_SEGMENTS];
    int nr_segments;
};


int
invirt_parse_memory_map(pid_t               pid,
                        struct memory_map * map);

void
invirt_print_memory_map(struct memory_map * map);

int
invirt_mmap_memory_map(struct memory_map * target_map,
                       int                 mmap_fd,
                       struct memory_map * self_map);

#endif /* __MEMORY_MAP_H__ */
