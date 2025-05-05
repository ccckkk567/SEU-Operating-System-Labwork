#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

/**
 * exec函数 - 加载并执行一个新的程序
 * 
 * 该函数实现了类似Unix中exec系统调用的功能，用于加载新的程序到内存中并执行。
 * 它会替换当前进程的地址空间，包括代码段、数据段和栈。这是进程创建的重要部分。
 * 
 * 执行流程：
 * 1. 打开可执行文件并检查ELF头
 * 2. 创建新的页表结构
 * 3. 加载程序段到内存（代码段和数据段）
 * 4. 为用户栈分配空间
 * 5. 在栈上设置命令行参数
 * 6. 设置程序入口点和栈指针
 * 7. 切换到新地址空间并释放旧地址空间
 * 
 * 空指针保护机制：
 * - 在地址空间的开始部分预留一页并设置为不可访问
 * - 这样当程序尝试解引用空指针时会触发页错误异常
 * 
 * @param path 要执行的程序路径
 * @param argv 命令行参数数组，以NULL结尾
 * @return 成功返回0（并开始执行新程序），失败返回-1
 */
int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];  // ustack用于构建用户栈的初始内容
  struct elfhdr elf;                       // ELF文件头结构，包含程序入口点等信息
  struct inode *ip;                        // 文件inode指针，用于访问程序文件
  struct proghdr ph;                       // 程序头结构，描述每个可加载段的信息
  pde_t *pgdir, *oldpgdir;                 // 新旧页目录指针，指向页表的顶层结构
  struct proc *curproc = myproc();         // 获取当前进程结构

  begin_op();  // 开始文件系统操作，确保文件系统的一致性

  // 根据路径名查找并打开要执行的程序文件
  // namei函数返回对应路径的inode结构
  if((ip = namei(path)) == 0){
    end_op();  // 结束文件系统操作
    cprintf("exec: fail\n");  // 打印错误信息
    return -1;  // 文件不存在，执行失败
  }
  ilock(ip);    // 锁定文件inode，防止并发访问导致不一致
  pgdir = 0;    // 初始化页目录指针为0

  // 读取并检查ELF文件头
  // readi函数从inode读取数据到指定内存位置
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;  // 读取失败，跳转到错误处理
  if(elf.magic != ELF_MAGIC)  // 检查ELF魔数是否正确，验证文件格式
    goto bad;  // 不是有效的ELF文件

  // 创建新的页表，用于新程序的地址空间
  // setupkvm分配一个页目录并设置内核部分的映射
  if((pgdir = setupkvm()) == 0)
    goto bad;  // 内存不足，页表创建失败

  // 加载程序到内存
  // 逐个处理ELF文件中的程序头，加载可执行段
  sz = 0;  // 初始化进程大小为0
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    // 读取第i个程序头
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)  // 只处理可加载段（代码段和数据段）
      continue;
    if(ph.memsz < ph.filesz)      // 内存大小应不小于文件大小
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)  // 检查地址是否溢出
      goto bad;
    // 为程序段分配虚拟内存空间
    // allocuvm扩展进程的地址空间并分配物理内存
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;  // 内存分配失败
    if(ph.vaddr % PGSIZE != 0)    // 程序段的虚拟地址必须页对齐
      goto bad;
    // 将程序段从文件加载到内存
    // loaduvm将数据从文件复制到指定的虚拟地址
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;  // 加载失败
  }
  iunlockput(ip);  // 解锁并释放文件inode
  end_op();        // 结束文件系统操作
  ip = 0;          // 清空文件指针，防止后续错误处理时重复释放

  // 使第一页不可访问，用于防止空指针解引用
  clearpteu(pgdir, (char*)0);

  // 分配用户栈
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + PGSIZE)) == 0)
    goto bad;
  sp = sz;

  // 准备用户栈，包括参数和环境变量
  // 将命令行参数从内核空间复制到用户栈
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)  // 检查参数数量是否超出限制
      goto bad;
    // 计算参数字符串在栈上的位置，并确保4字节对齐（&~3）
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    // 将参数字符串从内核空间复制到用户栈
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;  // 复制失败
    ustack[3+argc] = sp;  // 保存参数字符串的地址
  }
  ustack[3+argc] = 0;  // 参数指针数组的结束标记（NULL）

  // 在用户栈上设置初始帧
  ustack[0] = 0xffffffff;  // 假的返回PC（return-eip），程序不会返回，所以设为无效值
  ustack[1] = argc;        // 参数数量（argc）
  ustack[2] = sp - (argc+1)*4;  // argv指针数组的地址

  // 将栈的初始内容复制到用户栈
  sp -= (3+argc+1) * 4;  // 为ustack在栈上分配空间
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;  // 复制失败

  // 保存程序名用于调试和进程管理
  // 提取路径中的文件名部分
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // 提交到用户镜像：更新进程的页表和寄存器状态
  oldpgdir = curproc->pgdir;        // 保存旧页目录
  curproc->pgdir = pgdir;           // 设置新页目录
  curproc->sz = sz;                 // 更新进程大小
  curproc->tf->eip = elf.entry;     // 设置程序计数器指向入口点
  curproc->tf->esp = sp;            // 设置栈指针
  switchuvm(curproc);               // 切换到新地址空间（更新硬件页表寄存器）
  freevm(oldpgdir);                 // 释放旧地址空间占用的资源
  return 0;                         // 成功返回

 bad:
  // 错误处理：清理已分配的资源
  if(pgdir)
    freevm(pgdir);  // 释放已分配的页目录和页表
  if(ip){
    iunlockput(ip);  // 解锁并释放文件inode
    end_op();        // 结束文件系统操作
  }
  return -1;  // 执行失败返回
}
