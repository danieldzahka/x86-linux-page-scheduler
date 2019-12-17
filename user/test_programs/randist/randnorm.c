#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <math.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_rng.h>

#include <pg_sched.h>

#define REGION_SIZE (1 << 30)

static double
get_timediff(struct timeval * start,
	     struct timeval * end)
{
  double diff = 1000000*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec);
  return diff/1000000;
}

int
main (void)
{
  /*Init Number Generator*/
  gsl_rng * r = gsl_rng_alloc (gsl_rng_taus);
  if (r == NULL){
    puts("Couldnt Allocate Random Generator");
    return -1;
  }
    
  /*Open Device*/
  int status;
  int device_fd;
  int dev_open_flags = 0;
  struct timeval start, stop;

  device_fd = open(PG_SCHED_DEVICE_PATH, dev_open_flags);
  if (device_fd == -1){
    puts("Couldn't open " PG_SCHED_DEVICE_PATH "\n");
    return -1;
  }

  /*mmap region*/
  char * region;
  size_t length = REGION_SIZE;
  int    prot   = PROT_READ | PROT_WRITE;
  int    flags  = MAP_PRIVATE | MAP_ANONYMOUS;
  region = (char *) mmap(NULL, length, prot, flags, -1, 0);

  gettimeofday(&start, NULL);

  double mu    = length / 2;
  double sigma = length / 40;

  do {
    double norm = gsl_ran_gaussian(r, sigma) + mu;
    long off = lround(norm);
    if (off < 0) off = 0;
    if (off >= length) off = length - 1;
    region[off] = 'a';
    gettimeofday(&stop, NULL);
  } while (get_timediff(&start, &stop) < 100.0);


  status = close(device_fd);
  if (status){
    puts("Error closing " PG_SCHED_DEVICE_PATH "\n");
    return status;
  }

  status = munmap(region, length);
  if (status){
    puts("BAD UNMAP");
    return -1;
  }
  
  return 0;
}
