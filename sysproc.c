#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"


int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// JK
// Info System Call Here
// Takes one int param with value of 1, 2, 3
// Calls appropriate process call
int
sys_info(void)
{
  int input;
  int output;
  argint(0, &input);
  if (input == 1)
  {
    output = proccount();
  }
  else if (input == 2)
  {
    output = procsyscallcount();
  }
  
  else if (input == 3)
  {
    output = proccountpages();
  }
  else
  {
    return -1;
  }
  return output;
}

// JK
// Part 2 - Lottery Scheduler
// Passes program tickets to process
int
sys_settickets(void)
{
  int tickets;
  int process_num;
  argint(0, &tickets);
  argint(1, &process_num);
  proclottery(tickets, process_num);
  return tickets;
}

// JK
// Part 2 - Stride Scheduler
// Passes program tickets to process
int
sys_setstridetickets(void)
{
  int tickets;
  int process_num;
  argint(0, &tickets);
  argint(1, &process_num);
  procstridescheduler(tickets, process_num);
  return tickets;
}