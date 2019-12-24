#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#define N 1024

static void
print_timediff(struct timeval * start,
	       struct timeval * end)
{
  double diff = 1000000*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec);
  printf("Time: %f s\n", diff/1000000);
}

static void
matrix_multiply(int * A,
		int * B,
		int * C)
{
  for (int i = 0; i < N; ++i)
    for (int j = 0; j < N; ++j)
      for (int k =0; k < N; ++k)
	C[i*N + j]+= A[i*N + k] + B[k*N + j];
}

int main(int argc, char **argv){
  int status;
  struct timeval start, stop;

  int *A, *B, *C;
  
  size_t length = N*N*sizeof(int);
  int    prot   = PROT_READ | PROT_WRITE;
  int    flags  = MAP_PRIVATE | MAP_ANONYMOUS;
  
  A = mmap(NULL, length, prot, flags, -1, 0);
  B = mmap(NULL, length, prot, flags, -1, 0);
  C = mmap(NULL, length, prot, flags, -1, 0);

  gettimeofday(&start, NULL);
  matrix_multiply(A,B,C);
  gettimeofday(&stop, NULL);

  print_timediff(&start, &stop);

  munmap(A, length);
  munmap(B, length);
  munmap(C, length);
    
  return 0;
}
