// System call numbers
#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_pipe    4
#define SYS_read    5
#define SYS_kill    6
#define SYS_exec    7
#define SYS_fstat   8
#define SYS_chdir   9
#define SYS_dup    10
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_sleep  13
#define SYS_uptime 14
#define SYS_open   15
#define SYS_write  16
#define SYS_mknod  17
#define SYS_unlink 18
#define SYS_link   19
#define SYS_mkdir  20
#define SYS_close  21
#define SYS_getreadcount 22







/*

Xv6-Syscall/src/syscall.h是xv6操作系统中的一个重要头文件，它定义了系统调用号（System call numbers）。系统调用是用户程序与操作系统内核交互的接口。每个系统调用都被分配了一个唯一的编号，用于在内核中识别用户程序请求执行的具体系统调用。

该文件中的内容主要包括：

1. 基本系统调用的编号定义，例如：
   - `SYS_fork (1)`: 创建新进程
   - `SYS_exit (2)`: 终止当前进程
   - `SYS_wait (3)`: 等待子进程结束
   - `SYS_read (5)`: 从文件读取数据
   - `SYS_write (16)`: 向文件写入数据
   - `SYS_open (15)`: 打开文件
   - `SYS_close (21)`: 关闭文件

2. 自定义系统调用的编号，如：
   - `SYS_getreadcount (22)`: 这是一个自定义的系统调用，从名称看应该是用于获取系统中read调用的次数

在xv6系统调用的实现过程中，当用户程序调用系统函数时，会通过特定的机制（如软中断）陷入内核，内核根据这些编号来分派到对应的系统调用处理函数。这些编号在syscall.c文件中通常会与对应的处理函数通过数组或其他方式映射起来。

总结来说，syscall.h定义了用户空间和内核空间之间通信的接口编号，是操作系统实现系统调用机制的重要组成部分。您可以看到，该文件已经包含了一个自定义系统调用`SYS_getreadcount`，这可能是操作系统实验中添加的新功能，用于统计系统中read系统调用的使用次数。
*/