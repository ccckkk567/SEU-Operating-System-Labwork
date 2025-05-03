/**
 * MapReduce框架实现
 * 该文件实现了一个简化版的MapReduce框架，支持多线程并行处理数据
 * MapReduce是一种编程模型，用于大规模数据集的并行运算处理
 */
#include "mapreduce.h"

#include "utils.h"

#include <pthread.h>  // 线程库
#include <sched.h>    // 线程调度
#include <signal.h>   // 信号处理
#include <stdio.h>    // 标准输入输出
#include <stdlib.h>   // 标准库函数
#include <string.h>   // 字符串操作
#include <sys/types.h>

// 全局变量声明
pthread_mutex_t*    partition_locks;  // 分区锁数组，用于保护每个分区的数据访问
struct partition_t* partitions;       // 分区数组，存储中间结果
int                 num_partitions;   // 分区数量
Mapper              mapper;           // 映射函数指针
Reducer             reducer;          // 归约函数指针
pthread_t*          pthreads;         // 线程数组，-1表示线程可用

/**
 * 映射器参数结构体
 * 用于向映射线程传递参数
 */
struct mapper_arg_t {
    int   id;   // 线程ID
    char* arg;  // 输入参数（文件名）
};

/**
 * 归约器参数结构体
 * 用于向归约线程传递参数
 */
struct reducer_arg_t {
    int   id;            // 线程ID
    char* key;           // 要处理的键
    int   partition_id;  // 分区ID
};

/**
 * 获取指定键的下一个值
 * 
 * @param key 要获取值的键
 * @param partition_number 分区编号
 * @return 返回与键对应的下一个值，如果没有更多值则返回NULL
 */
char* MR_GetNext(char* key, int partition_number) {
    // 获取分区头节点
    struct info_node_t* info_ptr = partitions[partition_number].info_head;
    // 遍历查找匹配的键
    for (; info_ptr != NULL; info_ptr = info_ptr->next) {
        if (strcmp(info_ptr->info, key) == 0) {
            // 如果该键已被处理完成，返回NULL
            if (info_ptr->proceed == 1)
                return NULL;
            // 遍历数据节点查找未处理的值
            struct data_node_t* data_ptr = info_ptr->data;
            for (; data_ptr != NULL; data_ptr = data_ptr->next) {
                if (data_ptr->proceed == 0) {
                    // 标记为已处理并返回该值
                    data_ptr->proceed = 1;
                    return data_ptr->value;
                }
            }
            // 所有值都已处理，标记键为已处理
            info_ptr->proceed = 1;
            return NULL;
        }
    }
    // 未找到匹配的键
    return NULL;
}

/**
 * 映射器线程适配函数
 * 将用户定义的映射函数包装为pthread可用的函数
 * 
 * @param arg 映射器参数结构体
 * @return NULL
 */
void* MR_MapperAdapt(void* arg) {
    struct mapper_arg_t* pass_arg = ( struct mapper_arg_t* )arg;
    pthread_detach(pthread_self());  // 将线程分离，结束时自动回收资源
    mapper(pass_arg->arg);          // 调用用户定义的映射函数
    pthreads[pass_arg->id] = -1;    // 标记线程为可用状态
    return NULL;
}

/**
 * 归约器线程适配函数
 * 将用户定义的归约函数包装为pthread可用的函数
 * 
 * @param arg 归约器参数结构体
 * @return NULL
 */
void* MR_ReducerAdapt(void* arg) {
    struct reducer_arg_t* pass_arg = ( struct reducer_arg_t* )arg;
    pthread_detach(pthread_self());  // 将线程分离，结束时自动回收资源
    reducer(pass_arg->key, MR_GetNext, pass_arg->partition_id);  // 调用用户定义的归约函数
    pthreads[pass_arg->id] = -1;    // 标记线程为可用状态
    return NULL;
}

/**
 * 默认哈希分区函数
 * 用于确定键应该分配到哪个分区
 * 
 * @param key 需要计算哈希值的键
 * @param num_partitions 分区数量
 * @return 键的哈希值对应的分区索引
 */
unsigned long MR_DefaultHashPartition(char* key, int num_partitions) {
    unsigned long hash = 5381;  // 初始哈希值
    int           c;
    // DJB哈希算法
    while ((c = *key++) != '\0')
        hash = hash * 33 + c;
    return hash % num_partitions;  // 取模确定分区
}

/**
 * MapReduce框架的主要执行函数
 * 协调执行整个MapReduce流程
 * 
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @param map 用户定义的映射函数
 * @param num_mappers 映射器线程数量
 * @param reduce 用户定义的归约函数
 * @param num_reducers 归约器线程数量
 * @param partition 用于确定键归属分区的函数（本实现中未使用）
 */
void MR_Run(int argc, char* argv[], Mapper map, int num_mappers, Reducer reduce, int num_reducers, Partitioner partition) {
    // 创建分区锁和分区数组
    num_partitions = num_reducers;

    partition_locks = ( pthread_mutex_t* )malloc(sizeof(pthread_mutex_t) * num_partitions);
    partitions      = ( struct partition_t* )malloc(sizeof(struct partition_t) * num_partitions);
    for (int i = 0; i < num_partitions; ++i) {
        pthread_mutex_t tmp     = PTHREAD_MUTEX_INITIALIZER;
        partition_locks[i]      = tmp;
        partitions[i].info_head = NULL;
    }

    int current_work = 1;        // 当前要处理的工作索引
    int total_work   = argc - 1; // 总工作数量

    // 设置映射函数和创建线程数组
    mapper   = map;
    pthreads = ( pthread_t* )malloc(sizeof(pthread_t) * num_mappers);

    // 初始化线程数组，-1表示线程可用
    for (int i = 0; i < num_mappers; ++i) {
        pthreads[i] = -1;
    }
    
    // 执行映射阶段
    // 动态分配工作给可用的线程
    while (current_work <= total_work) {
        for (int i = 0; i < num_mappers; ++i) {
            // 检查线程是否可用
            if (pthreads[i] == -1) {
                if (current_work <= total_work) {
                    // 创建并初始化映射参数
                    struct mapper_arg_t* pass_arg = ( struct mapper_arg_t* )malloc(sizeof(struct mapper_arg_t));
                    pass_arg->arg                 = argv[current_work++];
                    pass_arg->id                  = i;
                    // 创建新线程执行映射任务
                    pthread_create(&pthreads[i], NULL, MR_MapperAdapt, pass_arg);
                }
                continue;
            }
        }
    }
    
    // 等待所有映射器线程完成
    int all_finished = 0;
    while (all_finished == 0) {
        all_finished = 1;
        for (int i = 0; i < num_mappers; ++i) {
            if (pthreads[i] == -1)
                continue;
            else
                all_finished = 0;
        }
    }

    // 调试代码（注释掉）：打印分区内容
    // for (int i = 0; i < num_partitions; ++i) {
    //     if (partitions[i].info_head != NULL) {
    //         printf("partition: %d\n", i);
    //         struct info_node_t* info = partitions[i].info_head;
    //         for (; info != NULL; info = info->next) {
    //             printf("info: %s\n", info->info);
    //             struct data_node_t* data = info->data;
    //             for (; data != NULL; data = data->next) {
    //                 printf("data: %s\n", data->value);
    //             }
    //         }
    //     }
    // }

    // 设置归约函数
    reducer = reduce;
    free(pthreads);
    // 为归约阶段创建线程数组
    pthreads = ( pthread_t* )malloc(sizeof(pthread_t) * num_reducers);

    // 初始化归约线程数组
    for (int i = 0; i < num_reducers; ++i) {
        pthreads[i] = -1;
    }
    
    // 执行归约阶段
    int finished_reduce = 0;
    while (finished_reduce == 0) {
        finished_reduce = 1;
        for (int i = 0; i < num_reducers; ++i) {
            // 如果线程正在运行，继续检查下一个
            if (pthreads[i] != -1) {
                finished_reduce = 0;
                continue;
            }
            
            // 查找分区中未处理的键
            struct info_node_t* info_ptr = partitions[i].info_head;
            for (; info_ptr != NULL; info_ptr = info_ptr->next) {
                if (info_ptr->proceed == 0)
                    break;
            }
            
            // 如果当前分区所有键已处理完，继续下一个分区
            if (info_ptr == NULL) {
                continue;
            }
            
            // 找到未处理的键，创建新线程处理
            finished_reduce                = 0;
            struct reducer_arg_t* pass_arg = ( struct reducer_arg_t* )malloc(sizeof(struct reducer_arg_t));
            pass_arg->id                   = i;
            pass_arg->key                  = info_ptr->info;
            pass_arg->partition_id         = i;
            pthread_create(&pthreads[i], NULL, MR_ReducerAdapt, pass_arg);
        }
    }
}

/**
 * 生成键值对，将中间结果添加到对应分区
 * 由映射函数调用以产生中间结果
 * 
 * @param key 键
 * @param value 值
 */
void MR_Emit(char* key, char* value) {
    // 计算键应该分配到哪个分区
    unsigned long partition_index = MR_DefaultHashPartition(key, num_partitions);
    // 加锁保护分区数据访问
    pthread_mutex_lock(&partition_locks[partition_index]);
    
    // 查找是否已存在该键
    struct info_node_t* info_ptr = partitions[partition_index].info_head;
    for (; info_ptr != NULL; info_ptr = info_ptr->next) {
        if (strcmp(info_ptr->info, key) == 0) {
            // 键已存在，添加新值
            insert_data(info_ptr, value);
            pthread_mutex_unlock(&partition_locks[partition_index]); // 解锁
            return;
        }
    }
    
    // 键不存在，创建新键并添加值
    insert_info(&partitions[partition_index], key);
    insert_data(partitions[partition_index].info_head, value);
    // 解锁
    pthread_mutex_unlock(&partition_locks[partition_index]);
}
