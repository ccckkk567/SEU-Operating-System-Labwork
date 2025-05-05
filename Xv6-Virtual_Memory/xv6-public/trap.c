#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
// 中断描述符表（所有CPU共享）
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;  // 时钟滴答锁
uint ticks;  // 系统时钟滴答计数

// 初始化中断描述符表
// 设置所有中断和异常的处理函数入口
void
tvinit(void)
{
  int i;

  // 设置所有中断和异常的处理函数
  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  // 特别设置系统调用中断，允许用户态调用
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  // 初始化时钟滴答锁
  initlock(&tickslock, "time");
}

// 加载中断描述符表到CPU
void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
// 中断和异常处理函数
// tf: 包含中断发生时CPU状态的trap frame结构
void
trap(struct trapframe *tf)
{
  // 处理页面错误
  if(tf->trapno == T_PGFLT){
    // 获取导致页面错误的地址
    uint addr = rcr2();
    
    // 检查是否是空指针访问
    if(addr == 0){
      cprintf("pid %d %s: null pointer dereference at addr 0x%x\n",
              myproc()->pid, myproc()->name, addr);
      exit(); // 立即退出进程
    }
    
    // 检查是否是写保护错误
    if(tf->err & PTE_W){
      cprintf("pid %d %s: write to protected page at addr 0x%x\n",
              myproc()->pid, myproc()->name, addr);
      exit(); // 立即退出进程
    }
  }

  // 处理系统调用
  if(tf->trapno == T_SYSCALL){
    // 如果进程已被标记为killed，则退出
    if(myproc()->killed)
      exit();
    // 保存trap frame到进程结构
    myproc()->tf = tf;
    // 执行系统调用
    syscall();
    // 检查系统调用后进程是否被标记为killed
    if(myproc()->killed)
      exit();
    return;
  }

  // 根据中断号处理不同的中断
  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:  // 时钟中断
    if(cpuid() == 0){  // 只在CPU 0上处理时钟
      acquire(&tickslock);
      ticks++;  // 增加时钟计数
      wakeup(&ticks);  // 唤醒等待时钟的进程
      release(&tickslock);
    }
    lapiceoi();  // 发送EOI信号给LAPIC
    break;
  case T_IRQ0 + IRQ_IDE:  // IDE磁盘中断
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:  // IDE1中断
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:  // 键盘中断
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:  // 串口中断
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:  // 中断7
  case T_IRQ0 + IRQ_SPURIOUS:  // 伪中断
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:  // 处理其他异常
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      // 在内核态发生的异常，可能是内核bug
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    // 在用户态发生的异常，终止进程
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;  // 标记进程为killed
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  // 如果进程已被标记为killed且在用户态，强制退出
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  // 在时钟中断时强制进程让出CPU
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  // 检查进程在让出CPU后是否被标记为killed
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
