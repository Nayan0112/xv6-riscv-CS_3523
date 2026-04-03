#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "stat.h"

//#define log 0
#define NFRAMES ((PHYSTOP - KERNBASE) / PGSIZE)
#define MAX_SP 128
#define SWAP_BASE PHYSTOP

struct frame {
  uint8 is_used;
  struct proc* p;
  uint64 va;
  uint8 rb;
};

struct frametable_t {
  struct frame f[NFRAMES];
  struct spinlock lock;
  int clock_p;
};

extern struct frametable_t frametable;
extern uint8 swap_mask[MAX_SP];

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

struct queue pqueue[LEVELS];

int ticks_max[LEVELS];

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;


void
qinit(void)
{
  for(int i = 0; i < LEVELS; i++){
    pqueue[i].tail = 0;
  }
  ticks_max[0] = 2;
  for(int i = 1; i < LEVELS; i++){
    ticks_max[i] = ticks_max[i-1] * 2;
  }
}

// pushes a process to the designated queue
// the value in p->qlevel must be updated else
// it will be pushed to the prev assigned queue
void
push_proc(struct proc* p)
{
  //acquire(&p->lock);
  int level = p->qlevel;
  struct queue* q = &pqueue[level];
  acquire(&q->lock);
  int tail = q->tail;
  if(tail < NPROC){
    q->qproc[tail] = p;
    tail++;
  }
  q->tail = tail;
  release(&q->lock);
  //release(&p->lock);
}

//pops a process from the current queue
void
pop_proc(int level)
{
  struct queue* q = &pqueue[level];
  if(q->tail < 1) return;
  //acquire(&q->lock);
  if(q->tail > 0){
    for(int i = 1; i < q->tail; i++){
      q->qproc[i-1] = q->qproc[i];
    }
  }
  q->tail--;
  //release(&q->lock);
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->sysCount = 0;              //initialising with system calls  invoked to 0
  
  
  p->qlevel = 0;
  p->pticks = 0;
  for(int i = 0; i < LEVELS; i++){
    p->qticks[i] = 0;
  }
  p->tsched = 0;
  p->dSysCount = 0;

  p->page_evicted = 0;
  p->page_faults = 0;
  p->pages_swapped_in = 0;
  p->pages_swapped_out = 0;
  p->resident_pages = 0;

  //push_proc(p);
  
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  push_proc(p);

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz, np) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  //COPY SWAP SPACE TOO
  for(uint64 va = 0; va < np->sz; va += PGSIZE){
    pte_t *pte = walk(np->pagetable, va, 0);

    if(pte && (*pte & PTE_S)) {
      int swap_index = (*pte) >> 10;
      uint64 mem = (uint64) kalloc();
      if(mem == 0){
        mem = evict_page();
      }
      acquire(&frametable.lock);

      struct frame *f = &frametable.f[(mem - KERNBASE) / PGSIZE];
      f->is_used = 1;
      f->p = np;
      f->va = va;
      f->rb = 1;

      release(&frametable.lock);

      memmove((void*)mem, get_swap_addr(swap_index), PGSIZE);

      pte_t *child_pte = walk(np->pagetable, va, 1);
      *child_pte = PA2PTE(mem) | PTE_V | PTE_U | PTE_R | PTE_W;

    }else if(pte && (*pte & PTE_V)){

      uint64 pa = PTE2PA(*pte);
      acquire(&frametable.lock);
      struct frame *f = &frametable.f[(pa - KERNBASE) / PGSIZE];
      f->is_used = 1;
      f->p = np;
      f->va = va;
      f->rb = 1;
      release(&frametable.lock);
    }
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;

  push_proc(np);

  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}

void
SC_MLFQ(void)
{
  struct proc* p;
  struct cpu* c = mycpu();

  c->proc = 0;
  static int last_mod128 = 0;
  for(;;){
    intr_on();
    intr_off();

    //push all the processes to level 0 after 128 ticks
    acquire(&tickslock);
    uint gtick = ticks;
    release(&tickslock);

    if( cpuid() == 0 && 
        gtick > 0 &&
        gtick % 128 == 0 &&
        gtick != last_mod128){

      last_mod128 = gtick;
      //push all the process to level0
      /*
      NOTE: there is a slight chance during
          this global boost a program is in
          running state might get left out 
          and it might happen repeatatively 
          which might degrade the performance
          of the process.
          Thus it is modified such that we 
          dont deprive the running process
      */
      for(int i = 0; i < LEVELS; i++){
        struct queue* q = &pqueue[i];
        acquire(&q->lock);
        q->tail = 0;
        release(&q->lock);
      }


      for(p = proc; p < &proc[NPROC]; p++){
        acquire(&p->lock);
        if( p->state != UNUSED  &&
            p->state != ZOMBIE  &&
            p->state != SLEEPING){
              
          // reinit
          p->qlevel = 0;
          p->pticks = 0;
          p->dSysCount = p->sysCount;
          acquire(&pqueue[0].lock);
          if(pqueue[0].tail < NPROC){
            pqueue[0].qproc[pqueue[0].tail] = p;
            pqueue[0].tail++;
          } 
          release(&pqueue[0].lock);
        }
        release(&p->lock);
      }
    }

    int found = 0;

    for(int i = 0; i < LEVELS; i++){
      struct queue* q = &pqueue[i];
      acquire(&q->lock);
      
      // if queue is not empty push the process 
      // check if the process is runnable and 
      // schedule it 

      if(q->tail > 0){

#ifdef log
        /*
        [LOGGING]
        getting the queue details
        prints level wise queue of pids
        */
        acquire(&wait_lock);
        for(int i = 0; i < LEVELS; i++){
          printf("\n[");
          for(int j = 0; j < pqueue[i].tail; j++){
            printf("%d ", pqueue[i].qproc[j]->pid);
          }
          printf("]");
        }
        release(&wait_lock);

#endif
        p = q->qproc[0];

        pop_proc(i);
        release(&q->lock);

        acquire(&p->lock);
        
        if(p->state == RUNNABLE){
          // logging
#ifdef log

          printf("\n[SCHEDULER]\tcpuid:%d\tpid:%d\n",cpuid(), p->pid);
#endif

          p->state = RUNNING;
          c->proc = p;
          p->tsched++;
      
          swtch(&c->context, &p->context);

          c->proc = 0;
          found = 1;

          //going back to level 0 each successful run
          release(&p->lock);
          break;

        }else{

          release(&p->lock);
        }
      }else{
        release(&q->lock);
      }
    }

    if(found == 0){
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

void
event_tick(struct proc* p)
{

  int should_yield = 0;

  acquire(&p->lock);
  int ptick = ++(p->pticks);
  int level = p->qlevel;
  p->qticks[level]++;
  int delta_S = p->sysCount - p->dSysCount;

  // used up the current time slice alloted
  // for the given pq level

  //logging
#ifdef log
  printf("\n[EVENT TICK]\tcpuid:%d\tpid:%d\ttick:%d\tds:%d\tlevel:%d\tqlt:%d", cpuid(), p->pid, p->pticks, delta_S, level, p->qticks[level]);
#endif

  if(p->pticks >= ticks_max[level]){
    /*
    updating the ticks after the process uses up all its time slice
    reinit the dysyscall to the current num of syscalls so that later
    delta_S can be caluclated
    */
    p->pticks = 0;
    p->dSysCount = p->sysCount;
    if(delta_S < ptick && level < (LEVELS)-1){
      //demote
      p->qlevel++;
    }
    // p->state = RUNNABLE;
    // push_proc(p);
    // sched();
    should_yield = 1;

  }else{
    struct queue* q;
    for(int i = 0; i < level; i++){
      q = &pqueue[i];
      acquire(&q->lock);
      if(q->tail){
        should_yield = 1;
        release(&q->lock);
        break;
      }
      release(&q->lock);
    }
  }

  if(should_yield){
    p->state = RUNNABLE;
    push_proc(p);
    sched();
  }
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        push_proc(p);
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

int
kgetppid(void)
{
  struct proc *pp = myproc();
  //if the proc has no parents or
  //is init itself then it should
  //return -1

  if(pp == initproc){
    return -1;
  }

  acquire(&wait_lock);

  //Since the kkill() command does not 
  //call the reparent() on the spot
  //and waits for parent to exit in 
  //some cases the parent might already 
  //have been killed and the child 
  //calls getppid(), that might return the
  //killed parent pid which is incorrect.
  
  if( pp->parent == initproc ||
      killed(pp->parent) == 1){
  
    release(&wait_lock);
    return -1;
  }
  int ppid = pp->parent->pid;
  release(&wait_lock);
  return ppid;
}


int
kgetnumchild(uint64 addr)
{
  struct proc *cp;
  int numKids;
  struct proc *p = myproc();

  acquire(&wait_lock);
  //making sure the child isn't in exit() or switch()
  //scan through table looking for the children
  numKids = 0;
  for(cp = proc; cp < &proc[NPROC]; cp++){
    if( cp->parent == p){
      acquire(&cp->lock);
      if( cp->killed != 1 &&
          cp->state != ZOMBIE
        )
        numKids++;
      release(&cp->lock);
    }
  }
  release(&wait_lock);
  return numKids;
}

int 
kgetchildsyscount(int PID)
{
  struct proc *cp;
  struct proc *pp = myproc();
  int count = -1;
  acquire(&wait_lock);
  for(cp = proc; cp < &proc[NPROC]; cp++){
    if( 
        cp->parent == pp
      ){
      //locking the process to check if it is 
      //in zombie state and preserve the syscalls
      acquire(&cp->lock);
      if( cp->pid == PID ){
        count = cp->sysCount;
        release(&cp->lock);
        break;
      }
      release(&cp->lock);
    }
  }
  release(&wait_lock);
  return count;
}

int
kgetmlfqinfo(int pid, uint64 info_ptr)
{
  int found = 0;
  struct proc* p;
  struct mlfqinfo info;
  acquire(&wait_lock);
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      info.level = p->qlevel;
      info.times_scheduled = p->tsched;
      info.total_syscalls = p->sysCount;
      for(int i = 0; i < LEVELS; i++){
        info.ticks[i] = p->qticks[i];
      }
      found = 1;
      release(&p->lock);
      break;
    }
    release(&p->lock);
  }
  release(&wait_lock);

  if(!found) return -1;

  if(copyout(myproc()->pagetable, info_ptr, (char*)&info, sizeof(info))< 0){
    return -1;
  }
  return 0;
}


int
kgetvmstats(int pid, uint64 info_ptr)
{
  int found = 0;
  struct proc* p;
  struct vmstats info;
  acquire(&wait_lock);
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->pid == pid && p->state != UNUSED){
      info.page_faults = p->page_faults;
      info.page_evicted = p->page_evicted;
      info.pages_swapped_in = p->pages_swapped_in;
      info.pages_swapped_out = p->pages_swapped_out;
      info.resident_pages = p->resident_pages;

      found = 1;
      break;
    }
  }
  release(&wait_lock);

  if(!found) return -1;

  if(copyout(myproc()->pagetable, info_ptr, (char*)&info, sizeof(info))< 0){
    return -1;
  }
  return 0;
}