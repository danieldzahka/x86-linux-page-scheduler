#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

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
