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

#define REGION_SIZE (1 << 29)

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

  double mu1    = length / 5;
  double mu2    =  2*mu1;
  double mu3    = 3*mu1;
  double mu4    = 4*mu1;
  double sigma = length / 40;

  int count = 0;
  do {
    double norm = gsl_ran_laplace(r, sigma) + mu1;
    double norm2 = gsl_ran_laplace(r, sigma) + mu2;
    double norm3 = gsl_ran_laplace(r, sigma) + mu3;
    double norm4 = gsl_ran_laplace(r, sigma) + mu4;	    
    long off = lround(norm);
    long off2 = lround(norm2);
    long off3 = lround(norm3);
    long off4 = lround(norm4);
    if (off < 0) off = 0;
    if (off >= length) off = length - 1;
    if (off2 < 0) off2 = 0;
    if (off2 >= length) off2 = length - 1;
    if (off3 < 0) off3 = 0;
    if (off3 >= length) off3 = length - 1;
    if (off4 < 0) off4 = 0;
    if (off4 >= length) off4 = length - 1;

    region[off] = 'a';
    region[off2] = 'a';
    region[off3] = 'a';
    region[off4] = 'a';

    if (count++ > 5000){
      usleep(500000);
      count = 0;
    }
    gettimeofday(&stop, NULL);
  } while (get_timediff(&start, &stop) < 300.0);


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
