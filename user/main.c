#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pg_sched.h>

int main(int argc, char **argv){
  int status;
  int device_fd;
  int dev_open_flags = 0;

  device_fd = open(PG_SCHED_DEVICE_PATH, dev_open_flags);
  if (device_fd == -1){
    puts("Couldn't open " PG_SCHED_DEVICE_PATH "\n");
    return -1;
  }
  
  return 0;
}
