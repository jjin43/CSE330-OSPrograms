#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>

#define SYS_justin_jin 548

int main(int argc, char **argv){

  printf("This is the new system call Justin Jin implemented");
  long res = syscall(SYS_justin_jin);
  return res;
  
}
