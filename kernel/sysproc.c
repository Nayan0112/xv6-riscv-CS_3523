#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//adding hello()
uint64
sys_hello(void)
{
  const char* s = "hello from the kernel!\n";
  printf("%s", s);
  return 0;
}

uint64
sys_getppid(void){
  //all the orphans are adopted by INIT
  //so it doesnt matter if we have to check
  //if the parent is dead or not 
  //one possible problem might be at the time of 
  //execution the parent might have been active 
  //but later got killed?
  return kgetppid();
}

uint64
sys_getnumchild(void){
  uint64 p;
  argaddr(0, &p);
  return kgetnumchild(p);
}

uint64
sys_getsyscount(void){
  return myproc()->sysCount;
}

uint64
sys_getchildsyscount(void){

  int pid;
  argint(0, &pid);
  return kgetchildsyscount(pid);
}

uint64
sys_getpid2(void){
  return myproc()->pid;
}

uint64
sys_getpinfo(void){
  struct proc* p = myproc();
  acquire(&p->lock);
  printf("\n --- PID:%d STATS ---\n", p->pid);
  printf("Total number of ticks per qlevel\n");
  for(int i = 0; i < LEVELS; i++){
    printf("LEVEL:%d\t->%d\n", i, p->qticks[i]);
  }
  printf("Number of times rescheduled:%d\n", p->tsched);
  printf("Number of syscount:%d\n", p->sysCount);
  release(&p->lock);
  return 0;
}

uint64
sys_getlevel(void){
  return myproc()->qlevel;
}

uint64
sys_getmlfqinfo(void){
  int pid;
  uint64 info_ptr;
  argint(0, &pid);
  argaddr(1, &info_ptr);
  return kgetmlfqinfo(pid, info_ptr);
}