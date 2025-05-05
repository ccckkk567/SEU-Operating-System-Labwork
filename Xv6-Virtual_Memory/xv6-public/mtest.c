#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "mmu.h"

/**
 * 内存保护测试程序
 * 
 * 该程序测试mprotect和munprotect系统调用的功能：
 * 1. 分配一页内存
 * 2. 写入初始值
 * 3. 设置内存保护
 * 4. 尝试写入（应该失败）
 * 5. 取消保护
 * 6. 再次写入
 */
int main(int argc, char *argv[]) {
    // 分配一页内存
    char* val = sbrk(0);
    sbrk(PGSIZE);

    // 写入初始值
    *val = 5;
    printf(1, "Start at %d\n", *val);

    // 设置内存保护
    mprotect((void*)val, 1);
    //munprotect((void*)val, 1);
    // 尝试写入（应该失败）
    *val = 10;
    printf(1, "Now is %d\n", *val);

    exit();
}