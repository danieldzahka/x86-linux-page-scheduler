#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <pg_sched.h>

static void
touch_pages(void * region,
	    int    pages,
	    int    pg_offset)
{
    for (int i = 0; i < pages; ++i){
	char * start = ((char*)region) + (pg_offset + i)*(1<<12);
	*start = 'd'; /*Do Touch*/
    }
}

static void *
mmap_pages(int pages)
{
    printf("PAGE SIZE: %ld\n", sysconf(_SC_PAGE_SIZE));
    
    void * region;
    size_t length = pages * (1<<12);
    int    prot   = PROT_READ | PROT_WRITE;
    int    flags  = MAP_PRIVATE | MAP_ANONYMOUS;
    region = mmap(NULL, length, prot, flags, -1, 0);

    return region;
}

int main(int argc, char **argv){
  int status;
  int device_fd;
  int dev_open_flags = 0;

  device_fd = open(PG_SCHED_DEVICE_PATH, dev_open_flags);
  if (device_fd == -1){
    puts("Couldn't open " PG_SCHED_DEVICE_PATH "\n");
    return -1;
  }

  status = ioctl(device_fd, PG_SCHED_SCAN_PT, NULL);
  if (status == -1){
    puts("IOCTL ERROR\n");
    return status;
  }

  void * region = mmap_pages(100);

  status = ioctl(device_fd, PG_SCHED_SCAN_PT, NULL);
  if (status == -1){
    puts("IOCTL ERROR\n");
    return status;
  }

  touch_pages(region, 50, 0);  /*  0 -> 49*/

  status = ioctl(device_fd, PG_SCHED_SCAN_PT, NULL);
  if (status == -1){
      puts("IOCTL ERROR\n");
      return status;
  }

  touch_pages(region, 50, 50); /* 50 -> 99*/

  status = ioctl(device_fd, PG_SCHED_SCAN_PT, NULL);
  if (status == -1){
    puts("IOCTL ERROR\n");
    return status;
  }

  status = munmap(region, 100 * (1<<12));
  if (status){
    puts("BAD UNMAP");
    return -1;
  }

  status = ioctl(device_fd, PG_SCHED_SCAN_PT, NULL);
  if (status == -1){
    puts("IOCTL ERROR\n");
    return status;
  }
    
  status = close(device_fd);
  if (status){
    puts("Error closing " PG_SCHED_DEVICE_PATH "\n");
    return status;
  }
  
  return 0;
}
