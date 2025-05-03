#pragma once

#include "pstat.h"
#include "spinlock.h"

// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
/**
 * 进程控制块结构体 - 存储进程的所有相关信息
 * 在xv6中，每个进程都由一个proc结构体表示，包含了进程的所有状态信息
 */
struct proc {
  uint sz;                     // 进程内存大小（字节数）
  pde_t* pgdir;                // 页表指针，指向进程的页目录
  char *kstack;                // 内核栈底指针，每个进程都有自己的内核栈
  enum procstate state;        // 进程状态（UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE）
  int pid;                     // 进程ID，唯一标识一个进程
  struct proc *parent;         // 父进程指针，指向创建此进程的进程
  struct trapframe *tf;        // 陷阱帧指针，保存进入内核前的用户上下文
  struct context *context;     // 上下文指针，用于进程切换（swtch）
  void *chan;                  // 如果不为零，表示进程在此通道上睡眠等待
  int killed;                  // 如果不为零，表示进程已被标记为要终止
  struct file *ofile[NOFILE];  // 打开的文件数组，存储进程打开的所有文件
  struct inode *cwd;           // 当前工作目录的inode指针
  char name[16];               // 进程名称，主要用于调试

  // 彩票调度(Lottery Scheduling)相关字段
  int inuse;                   // 标记此结构体是否正在使用
  int tickets;                 // 进程持有的彩票数量，决定获得CPU时间的概率
  int ticks;                   // 进程已使用的CPU时钟周期数，用于统计和性能分析
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

struct ptable_t {
  struct spinlock lock;
  struct proc proc[NPROC];
};
extern struct ptable_t ptable;

void setproctickets(struct proc*, int);
