/**
 * random.c - 伪随机数生成器实现
 * 
 * 此文件实现了一个简单的线性同余伪随机数生成器(Linear Congruential Generator, LCG)
 * 用于xv6操作系统中的彩票调度算法(Lottery Scheduling)
 * 
 * LCG算法公式: X_(n+1) = (a * X_n + c) mod m
 * 其中:
 * - a是乘数(multiplier)
 * - c是增量(increment)
 * - m是模数(modulus)
 * - X_0是种子(seed)
 */
#include "random.h"

// 线性同余生成器的参数
unsigned int ran_A = 1103515245;  // 乘数(multiplier)
unsigned int ran_B = 12345;       // 增量(increment)
unsigned int ran_status = 5167495;  // 当前随机数状态，初始值作为默认种子

/**
 * 设置随机数生成器的种子
 * 
 * @param seed 随机数生成器的种子，用于初始化随机数序列
 *             不同的种子会产生不同的随机数序列
 */
void srand(unsigned int seed) {
    ran_status = seed;
}

/**
 * 生成下一个伪随机数
 * 
 * 使用线性同余算法计算下一个随机数:
 * ran_status = (ran_A * ran_status + ran_B) mod 2^32
 * 
 * 注: 在32位系统中，unsigned int溢出会自动取模2^32
 * 
 * @return 返回生成的伪随机数
 */
unsigned int rand() {
    unsigned long long tmp = ran_A * ran_status + ran_B;  // 使用64位避免中间计算溢出
    ran_status = (unsigned int)tmp;  // 截断为32位，相当于对2^32取模
    return ran_status;
}
