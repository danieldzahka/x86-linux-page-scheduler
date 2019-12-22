/*
 * This file is part of the invirt project developed at Washington
 * University in St. Louis
 *
 * This is free software.  You are permitted to use, redistribute, and
 * modify it as specified in the file "LICENSE.md".
 */

/*
 * Code for parsing and updating virtual memory maps 
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "memory_map.h"

#define MAPS_BUF_SZ PAGE_SIZE


/*
 * snapshot a text file
 */
static int
__take_snapshot(char * in_fname,
                char * out_fname)
{
    int infd, outfd, status;
    ssize_t inbytes, outbytes;
    char buf[PAGE_SIZE];

    status = -1;

    infd = open(in_fname, O_RDONLY);
    if (infd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", in_fname, strerror(errno));
        return -1;
    }

    outfd = open(out_fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", out_fname, strerror(errno));
        goto out_outfd;
    }

    while ((inbytes = read(infd, &buf, sizeof(buf))) > 0) {
        outbytes = write(outfd, &buf, inbytes);
        if (outbytes != inbytes) {
            fprintf(stderr, "Could not copy data to snapshot: %s\n", strerror(errno));
            goto out_copy;
        }
    }

    status = 0;

out_copy:
    close(outfd);

    if (status != 0)
        unlink(out_fname);

out_outfd:
    close(infd);

    return status;
}

/* static unsigned long */
/* __str_to_prot(char str[4]) */
/* { */
/*     unsigned long prot = 0; */

/*     if (str[0] == 'r') */
/*         prot |= INV_FLAG_READ; */

/*     if (str[1] == 'w') */
/*         prot |= INV_FLAG_WRITE; */

/*     if (str[2] == 'x') */
/*         prot |= INV_FLAG_EXEC; */

/*     return prot; */
/* } */

/* static unsigned long */
/* __inv_prot_to_mmap(unsigned long prot) */
/* { */
/*     unsigned long mmap_prot = 0; */

/*     if (prot & INV_FLAG_READ) */
/*         mmap_prot |= PROT_READ; */

/*     if (prot & INV_FLAG_WRITE) */
/*         mmap_prot |= PROT_WRITE; */

/*     if (prot & INV_FLAG_EXEC) */
/*         mmap_prot |= PROT_EXEC; */

/*     return mmap_prot; */
/* } */

/* /\* */
/*  * parse a memory map from a text file into a shadow map  */
/*  *\/ */
/* static int  */
/* __parse_snapshot(char              * filename, */
/*                  struct memory_map * map) */
/* { */
/*     unsigned long start, end, off, ino; */
/*     char line[MAPS_BUF_SZ]; */
/*     char buf[MAPS_BUF_SZ]; */
/*     char * tmp; */
/*     int ret; */
/*     FILE * f; */

/*     f = fopen(filename, "r"); */
/*     if (f == NULL) { */
/*         fprintf(stderr, "Failed to open snapshot\n"); */
/*         return -1; */
/*     } */

/*     while (1) { */
/*         unsigned long flags = 0; */
/*         char prot[4]; */

/*         if (map->nr_segments == MAX_SEGMENTS) { */
/*             fprintf(stderr, "Too many segments .. increase MAX_SEGMENTS\n"); */
/*             fflush(stderr); */
/*             break; */
/*         } */

/*         tmp = fgets(line, MAPS_BUF_SZ, f); */
/*         if (!tmp) */
/*             break; */

/*         buf[0] = '\0'; */
/*         ret = sscanf(line, "%lx-%lx %4s %lx %*s %ld %255s", &start, &end, prot, &off, &ino, buf); */
/*         if ((ret < 5) || (ret > 6)) { */
/*             fprintf(stderr, "Couldn't parse %s: %s\n", filename, strerror(errno)); */
/*             fclose(f); */
/*             return -1; */
/*         } */

/*         flags = __str_to_prot(prot); */

/*         if (strcmp(buf, "[heap]") == 0) */
/*             flags |= INV_FLAG_HEAP; */

/*         else if (strcmp(buf, "[stack]") == 0) */
/*             flags |= INV_FLAG_STACK; */

/*         else if (strcmp(buf, "[vvar]") == 0) */
/*             flags |= INV_FLAG_VVAR; */

/*         else if (strcmp(buf, "[vdso]") == 0) */
/*             flags |= INV_FLAG_VDSO; */

/*         else if (strcmp(buf, "[vsyscall]") == 0) */
/*             flags |= INV_FLAG_VSYSCALL; */

/*         /\* else if (strcmp(buf, INVIRT_DEVICE_PATH) == 0) *\/ */
/*         /\*     flags |= INV_FLAG_INVIRT; *\/ */

/*         assert((end - start) % PAGE_SIZE == 0); */

/*         map->segments[map->nr_segments].start = start; */
/*         map->segments[map->nr_segments].end   = end; */
/*         map->segments[map->nr_segments].flags = flags; */
/*         map->nr_segments++; */
/*     } */

/*     fclose(f); */
/*     return 0; */
/* } */

static int
dump_raw_snapshot(char * filename)
{
    unsigned long start, end, off, ino;
    char line[MAPS_BUF_SZ];
    char buf[MAPS_BUF_SZ];
    char * tmp;
    int ret;
    FILE * f;

    f = fopen(filename, "r");
    if (f == NULL) {
	fprintf(stderr, "Failed to open snapshot\n");
	return -1;
    }

    while (1) {
	unsigned long flags = 0;
	char prot[4];

	tmp = fgets(line, MAPS_BUF_SZ, f);
	if (!tmp)
	    break;

	buf[0] = '\0';
	ret = sscanf(line, "%lx-%lx %4s %lx %*s %ld %255s", &start, &end, prot, &off, &ino, buf);
	if ((ret < 5) || (ret > 6)) {
	    fprintf(stderr, "Couldn't parse %s: %s\n", filename, strerror(errno));
	    fclose(f);
	    return -1;
	}

	printf("%lx-%lx %4s %s\n", start, end, prot, buf);
    }

    fclose(f);
    return 0;
}

int 
pg_sched_dump_memory_map(pid_t pid)
{
    char in_fname[64], out_fname[64];
    int status;

    snprintf(in_fname, 64, "/proc/%d/maps", pid);
    snprintf(out_fname, 64, PG_SCHED_TMP_DIR "invirt-%d-snapshot", pid);

    status = __take_snapshot(in_fname, out_fname);
    if (status)
        return status;

    printf("##### Pid %d's memory map #####\n", pid);
    status = dump_raw_snapshot(out_fname);
    unlink(out_fname);

    return status;
}

/* static char * */
/* flags_to_str(unsigned long flags) */
/* { */
/*     if (flags & INV_FLAG_HEAP) */
/*         return " [heap]"; */
/*     else if (flags & INV_FLAG_STACK) */
/*         return " [stack]"; */
/*     else if (flags & INV_FLAG_VVAR) */
/*         return " [vvar]"; */
/*     else if (flags & INV_FLAG_VDSO) */
/*         return " [vdso]"; */
/*     else if (flags & INV_FLAG_VSYSCALL) */
/*         return " [vsyscall]"; */
/*     else if (flags & INV_FLAG_INVIRT) */
/*         return " [invirt]"; */
/*     else */
/*         return ""; */
/* } */

/* static bool */
/* segments_intersect(struct memory_segment * sega, */
/*                    struct memory_segment * segb) */
/* { */
/*     struct memory_segment * first, * second; */

/*     if (sega->start < segb->start) { */
/*         first = sega; second = segb; */
/*     } else { */
/*         first = segb; second = sega; */
/*     } */
/*     return (second->start <= first->end) ? true : false; */
/* } */

/* static bool */
/* segment_intersects_map(struct memory_segment  * seg, */
/*                        struct memory_map      * map, */
/*                        struct memory_segment ** map_seg) */
/* { */
/*     int i; */

/*     for (i = 0; i < map->nr_segments; ++i) { */
/*         if (segments_intersect(seg, &(map->segments[i]))) { */
/*             *map_seg = seg; */
/*             return true; */
/*         } */
/*     } */
/*     return false; */
/* } */

/* static int */
/* __mmap_regular_segment(struct memory_segment * seg, */
/*                        int                     mmap_fd) */
/* { */
/*     void * addr   = (void *)seg->start; */
/*     void * dest   = NULL; */
/*     size_t length = seg->end - seg->start; */
/*     int    prot   = __inv_prot_to_mmap(seg->flags); */
/*     int    flags  = MAP_SHARED | MAP_FIXED; */

/*     if (mmap_fd == -1) */
/*         flags |= MAP_ANONYMOUS; */

/*     dest = mmap(addr, length, prot, flags, mmap_fd, 0); */
/*     if (dest == MAP_FAILED) { */
/*         fprintf(stderr, "mmap() failed: %s\n", strerror(errno)); */
/*         fflush(stderr); */
/*         return -1; */
/*     } */

/*     assert(dest == addr); */
/*     return 0; */
/* } */


/* /\*  */
/*  * Parse virtual address space via /proc/<pid>/maps */
/*  * */
/*  * We don't use libc stream based IO calls directly on a FILE * associated with */
/*  * /proc/<pid>/maps. The reason is that, internally, these calls will allocate */
/*  * memory and perhaps expand the address space, which will show up in new */
/*  * address space within /proc/<pid>/maps. However these entries will be freed */
/*  * when the stream is closed, and if we try to relocate those entries in the */
/*  * future, we're going to have problems */
/*  * */
/*  * So instead we first create a snapshot of /proc/<pid>/maps to a separate text */
/*  * file, using only raw stdio calls that don't allocate memory internally. Then */
/*  * we process this text file using the more convenient stream-based interfaces */
/*  *\/ */
/* int  */
/* invirt_parse_memory_map(pid_t               pid, */
/*                         struct memory_map * map) */
/* { */
/*     char in_fname[64], out_fname[64]; */
/*     int status; */

/*     memset(map, 0, sizeof(struct memory_map)); */
/*     map->pid = pid; */

/*     snprintf(in_fname, 64, "/proc/%d/maps", pid); */
/*     snprintf(out_fname, 64, PG_SCHED_TMP_DIR "invirt-%d-snapshot", pid); */

/*     status = __take_snapshot(in_fname, out_fname); */
/*     if (status) */
/*         return status; */

/*     status = __parse_snapshot(out_fname, map); */
/*     unlink(out_fname); */

/*     return status; */
/* } */

/* /\* */
/*  * Print our invirt memory map */
/*  *\/ */
/* void */
/* invirt_print_memory_map(struct memory_map * map) */
/* { */
/*     int i; */

/*     printf("##### Pid %d's memory map #####\n", map->pid); */

/*     for (i = 0; i < map->nr_segments; i++) { */
/*         printf("  0x%lx ---- 0x%lx%s\n", */
/*             map->segments[i].start, */
/*             map->segments[i].end, */
/*             flags_to_str(map->segments[i].flags) */
/*         ); */
/*     } */

/*     fflush(stdout); */
/* } */

/* /\* */
/*  * Map a target process' memory (specified in 'target_map') into */
/*  * the local process address space */
/*  * */
/*  *   'target_map' is the memory map of the target */
/*  *   'invirt_fd' is an fd to invoke mmap() on (can be -1 to map /dev/zero) */
/*  *   'self_map' is our own memory map (can be NULL) */
/*  *\/ */
/* int */
/* invirt_mmap_memory_map(struct memory_map * target_map, */
/*                        int                 mmap_fd, */
/*                        struct memory_map * self_map) */
/* { */
/*     struct memory_map __self_map; */
/*     int i, status; */

/*     /\* user can pass null to self_map *\/ */
/*     if (self_map == NULL) { */
/*         status = invirt_parse_memory_map(getpid(), &__self_map); */
/*         if (status != 0) { */
/*             fprintf(stderr, "Unable to parse memory map: %s\n", strerror(errno)); */
/*             return status; */
/*         } */

/*         self_map = &__self_map; */
/*     } */

/*     for (i = 0; i < target_map->nr_segments; ++i) { */
/*         struct memory_segment * seg = &(target_map->segments[i]); */
/*         struct memory_segment * inter_seg = NULL; */

/*         /\* skip vsyscall segments *\/ */
/*         if (seg->flags & INV_FLAG_VSYSCALL) */
/*             continue; */

/*         if (segment_intersects_map(seg, self_map, &inter_seg)) { */
/*             /\* the only segment that's allowed to intersect is the stack *\/ */
/*             if (!(inter_seg->flags & INV_FLAG_STACK)) { */
/*                 fprintf(stderr, "Found non-stack intersecting segment in shadow address space!\n"); */
/*                 fflush(stderr); */
/*                 return -1; */
/*             } */

/*             fprintf(stderr, "Stack intersects; TODO: need to do a stack switch ...\n"); */
/*             fflush(stderr); */
/*             return -1; */
/*         } */

/*         status =__mmap_regular_segment(seg, mmap_fd); */
/*         if (status != 0) */
/*             return status; */
/*     } */

/*     /\* re-generate self_map *\/ */
/*     if (self_map != &__self_map) */
/*         (void)invirt_parse_memory_map(getpid(), self_map); */

/*     return 0; */
/* } */
