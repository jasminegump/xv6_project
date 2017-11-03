#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// JK
// My scheduler #defines that I change on compile time
#define STRIDE_SCHEDULER 1
#define LOTTERY_SCHEDULER 0
#define ROUND_ROBIN_SCHEDULER 0


struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// JK
// My variables
int typeofscheduler = 0;
int tottickets = 0;
int lastendingticket = 0;
int end_print = 1;
int global_scheduler_tick;

// JK
// Retrieved code for random number generator from here:
// https://stackoverflow.com/questions/822323/how-to-generate-a-random-number-in-c
static unsigned long int next = 1;
int rand(void)  /* RAND_MAX assumed to be 32767. */
{
    next = next * 1103515245 + 12345;
    return (unsigned)(next/65536) % 32768;
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // JK
  // For lottery scheduler, must initialize when allocating process
  p->tickets = 100;
  p->tick = 0;
  p->procnumtickets_low = 0;
  p->procnumtickets_high = 0;

  // JK
  // For stride scheduler, must initialize when allocating process
  p->stride_tickets = 1;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;


  // JK
  // For lottery and stride scheduler, must initialize when allocating process
  // Also for system call count, must initialize for new process
  p->tickets = 1;
  p->stride_tickets = 1;
  p->syscall_count = 0;
  
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  // JK
  // For system call count, must initialize for new process
  np->syscall_count = 0;

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }


  // JK
  // Following sections are for lottery and stride scheduler
  // It is in order to print out the ticks ratio when the first process
  // terminates.
  #if LOTTERY_SCHEDULER
    if((curproc->count_me_ticks == 1) && (end_print == 1))
    {
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if(p->count_me_ticks)
        {
          cprintf("prog%d with %d tickets: %d\n", p->prog_num, p->tickets, p->tick); 
        }
      }
      end_print = 0;
    }
  #endif
  #if STRIDE_SCHEDULER
  if((curproc->count_me_ticks == 1) && (end_print == 1))
  {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if(p->count_me_ticks)
      {
        cprintf("prog%d with %d tickets: %d\n", p->prog_num, p->stride_tickets, p->stride_ticks); 
      }
    }
    end_print = 0;
  }
  #endif

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  global_scheduler_tick = 0;

  c->proc = 0;

  // JK
  // The following is the original round robin scheduler
  #if (ROUND_ROBIN_SCHEDULER)
    for(;;)
    {
      // Enable interrupts on this processor.
      sti();

      // Loop over process table looking for process to run.
      acquire(&ptable.lock);
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&ptable.lock);
    }
  #endif

  // JK
  // The following is the lottery scheduler
  #if (LOTTERY_SCHEDULER)
    int winner;
    int low_bound_ticket;
    c->proc = 0;
    low_bound_ticket = 0;
  
    for(;;)
    {
      // Enable interrupts on this processor.
      sti();

      // Loop over process table looking for process to run.
      acquire(&ptable.lock);

      // This was used for the evaluation of the two scheduler phase
      global_scheduler_tick = global_scheduler_tick + 1;

      tottickets = 0;
      low_bound_ticket = 0;

      // Go through all runnable processes and re-arrange lower and upper ticket bounds
      // in order to generate random number
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
        if((p->state == RUNNABLE) || (p->state == RUNNING))
        {
          p->procnumtickets_low = low_bound_ticket;
          p->procnumtickets_high = p->procnumtickets_low  + p->tickets;
          tottickets = tottickets + p->tickets;
          low_bound_ticket = p->procnumtickets_high + 1;
        }
      }

      if (tottickets > 0)
      {
        // Calculate winner
        winner = (rand() % (tottickets + 1));

        // Go through processes looking for winner
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        {
          if((p->state != RUNNABLE) && (p->state != RUNNING))
            continue;
          if ((p->procnumtickets_high >= winner) && (p->procnumtickets_low <= winner))
          {
            // Increment count if selected winner
            p->tick = p->tick + 1;

            // Switch to chosen process.  It is the process's job
            // to release ptable.lock and then reacquire it
            // before jumping back to us.
            if (p->state != RUNNING)
            {
              c->proc = p;
              switchuvm(p);
              p->state = RUNNING;

              swtch(&(c->scheduler), p->context);
              switchkvm();
            
              // Process is done running for now.
              // It should have changed its p->state before coming back.
              c->proc = 0;
            }
          }
        }

      }
      release(&ptable.lock);
    }
  #endif

  // JK
  // The following is the stride scheduler
  #if (STRIDE_SCHEDULER)
  int stride_num_tickets;
  int current_min;
  stride_num_tickets = 10000;
  c->proc = 0;
  current_min = 1;  
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    // This was used for the evaluation of the two scheduler phase
    global_scheduler_tick = global_scheduler_tick + 1;

    // Go through process list looking for processes that are ready to run in stride
    // p->stride_tickets gets set when running system call for user program that uses stride scheduler
    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      p->stride_value = 0;

      if (p->stride_tickets > 1)
      {
        if((p->state == RUNNABLE) || (p->state == RUNNING))
        {
          // Calculate stride value
          p->stride_value = stride_num_tickets / p->stride_tickets;

          // Look for minimum stride pass value to run
          if (current_min >= p->stride_pass)
          {
            // If we're in here, we found the lowest stride pass value and process to run
            p->stride_pass = p->stride_value + p->stride_pass;
            p->stride_ticks = p->stride_ticks + 1;
            current_min = p->stride_pass;

            // Switch processes
            if (p->state != RUNNING)
            {
              c->proc = p;
              switchuvm(p);
              p->state = RUNNING;

              swtch(&(c->scheduler), p->context);
              switchkvm();

              // Process is done running for now.
              // It should have changed its p->state before coming back.
              c->proc = 0;
            }
          }

        }

      }
      // Otherwise run in normal round robin mode
      // This is so the init and sh can come up in round robin mode
      else
      {
          if(p->state != RUNNABLE)
            continue;

          // Switch to chosen process.  It is the process's job
          // to release ptable.lock and then reacquire it
          // before jumping back to us.
          c->proc = p;
          switchuvm(p);
          p->state = RUNNING;

          swtch(&(c->scheduler), p->context);
          switchkvm();

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
      }
    }
    release(&ptable.lock);
  } 
  #endif
}


// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// JK
// Part 1.1 - Count number of processes in system
// Goes through ptable and looks for any state that is not UNUSED and counts it
// output - int numprocess
//
int
proccount(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  int numprocess;
  numprocess = 0;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
    {
      numprocess = numprocess + 1;
     
    }
  }
  return numprocess;
}

// JK
// Part 1.2 - Count total number of system calls a process has done so far
// Retrieves current process' system call count
//
int
procsyscallcount(void)
{
  struct proc *curproc = myproc();
  return curproc -> syscall_count;
}

// JK
// Part 1.3 - Number of memory pages the current process is using
// Goes through looking for whatever current process is running and calculates 
// the page by dividing the process' size by page size
//
int
proccountpages(void)
{
  struct proc *p;
  uint numpages;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNING)
    {
      numpages = (p->sz)/PGSIZE;
      if ((p->sz) % PGSIZE)
      {
        numpages = numpages + 1;
      }
    }  
  }
  return numpages;
}

// JK
// Part 2 - Lottery Scheduler
// Sets up tickets for current process and its proc struct values needed for scheduler
void
proclottery(int tickets, int process_num)
{

  struct proc *curproc = myproc();
  acquire(&ptable.lock);
  global_scheduler_tick = 0;
  curproc->tickets = tickets;
  curproc->tick = 0;
  curproc->count_me_ticks = 1;
  curproc->prog_num = process_num;
  release(&ptable.lock);
}

// JK
// Part 2 - Stride Scheduler
// Sets up tickets for current process and its proc struct values needed for scheduler
void
procstridescheduler(int stride_tickets, int process_num)
{

  struct proc *curproc = myproc();
  acquire(&ptable.lock);
  global_scheduler_tick = 0;
  curproc->stride_tickets = stride_tickets;
  curproc->stride_pass = 0;
  curproc->stride_ticks = 0;
  curproc->count_me_ticks = 1;
  curproc->prog_num = process_num;
  release(&ptable.lock);
}


