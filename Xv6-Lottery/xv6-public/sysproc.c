#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "pstat.h"

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

/**
 * 设置进程彩票数量的系统调用实现
 * 
 * 此系统调用允许用户空间程序设置当前进程持有的彩票数量
 * 在彩票调度算法中，彩票数量直接影响进程获得CPU时间的几率
 * 彩票数量越多，被调度器选中的概率越高
 * 
 * @return 成功返回0，失败返回-1
 */
int sys_settickets(void) {
  int num_tickets;
  // 从用户空间获取参数：要设置的彩票数量
  argint(0, &num_tickets);
  // 检查彩票数量是否合法（必须为正数）
  if(num_tickets <= 0)
    return -1;
  
  // 获取进程表锁，防止并发访问导致的竞争条件
  acquire(&ptable.lock);
  // 设置当前进程的彩票数量
  setproctickets(myproc(), num_tickets);
  // 释放进程表锁
  release(&ptable.lock);
  return 0;
}

/**
 * 获取进程信息的系统调用实现
 * 
 * 此系统调用用于收集系统中所有进程的统计信息，
 * 包括彩票调度相关的数据，如进程ID、持有的彩票数量、
 * CPU使用时间（ticks）以及进程是否在使用中等
 * 
 * @return 成功返回0，失败返回-1
 */
int sys_getpinfo(void) {
  struct pstat* target;
  // 从用户空间获取参数：用于存储进程统计信息的数据结构指针
  argptr(0, (void*)&target, sizeof(*target));
  // 检查指针是否有效
  if(!target)
    return -1;

  // 获取进程表锁，确保在收集信息时进程状态不变
  acquire(&ptable.lock);
  struct proc* p;
  // 遍历进程表中的所有进程
  for(p = ptable.proc; p != &(ptable.proc[NPROC]); ++p) {
    // 计算当前进程在进程表中的索引
    const int index = p - ptable.proc;
    // 只收集活跃进程的信息（状态不为UNUSED）
    if(p->state != UNUSED) {
      // 填充进程统计信息
      target->pid[index] = p->pid;       // 进程ID
      target->ticks[index] = p->ticks;   // CPU使用时间
      target->inuse[index] = p->inuse;   // 是否在使用中
      target->tickets[index] = p->tickets; // 持有的彩票数量
    }
  }
  // 释放进程表锁
  release(&ptable.lock);
  return 0;
}