/**
 * xcheck.c - 文件系统一致性检查器
 * 
 * 该程序检查xv6文件系统镜像的一致性，类似于UNIX的fsck工具。
 * 它验证多种文件系统不变量并报告任何错误。
 */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define stat xv6_stat // 避免与主机的struct stat冲突

// 指定xv6路径！
#include "./xv6-public/fs.h"
#include "./xv6-public/param.h"
#include "./xv6-public/stat.h"
#include "./xv6-public/types.h"

int img_file; // 文件系统镜像的文件描述符

struct superblock sblock; // 超级块结构体
struct dinode cur_inode; // 当前处理的inode

/**
 * 错误检查1：检查inode类型是否有效
 * 
 * 算法:
 * 1. 检查inode的type字段值是否为有效的枚举值
 * 2. 有效值包括: 
 *    - 0 (未使用的inode)
 *    - T_DEV (设备文件)
 *    - T_DIR (目录)
 *    - T_FILE (普通文件)
 * 
 * 判断标准:
 * - 如果type字段为上述四种类型之一，则认为inode类型有效
 * - 否则判定为类型无效，返回错误
 * 
 * 错误影响:
 * 无效的inode类型会导致文件系统无法正确识别此inode对应的数据类型，
 * 可能会引起文件系统混乱，导致数据访问错误。
 * 
 * @param nd 要检查的inode
 * @return 如果类型无效返回1，否则返回0
 */
int error_check_1(struct dinode nd) {
    if(nd.type != 0 && nd.type != T_DEV && nd.type != T_DIR && nd.type != T_FILE) 
        return 1; // 发现无效类型
    return 0;
}

/**
 * 错误检查2：验证inode的所有地址指向有效的数据块
 * 
 * 算法:
 * 1. 计算文件系统中有效的数据块范围:
 *    - 下界：位图区域之后 (bitmap_end)
 *    - 上界：文件系统总大小 (fs_end)
 * 2. 检查inode的所有直接块指针是否在有效范围内
 * 3. 如果inode有间接块，读取间接块内容并检查其指向的所有数据块地址
 * 
 * 判断标准:
 * - 每个非零地址必须大于等于bitmap_end（位图末尾）
 * - 每个地址必须小于等于fs_end（文件系统末尾）
 * - 零地址(0)表示未使用的块，不做范围检查
 * 
 * 错误影响:
 * 如果inode指向无效区域，可能会导致:
 * 1. 读取系统元数据区(超级块、inode等)而破坏文件系统结构
 * 2. 访问文件系统界外的内存区域，造成程序崩溃
 * 3. 多个文件错误地共享数据块，导致数据损坏
 * 
 * @param nd 要检查的inode
 * @return 如果有无效地址返回1，否则返回0
 */
int error_check_2(struct dinode nd) {
    uint bitmap_end = sblock.bmapstart + sblock.size / (8 * BSIZE); // 位图结束位置
    uint fs_end = sblock.size - 1; // 文件系统结束位置

    if(sblock.size % (8 * BSIZE) != 0)
        bitmap_end++; // 如果有余数，向上取整
    
    // 循环检查所有直接指针
    for(uint i = 0; i < NDIRECT + 1; ++i) 
        if(nd.addrs[i] != 0 && (nd.addrs[i] < bitmap_end || nd.addrs[i] > fs_end)) 
            return 1; // 地址超出有效范围
        
    // 检查间接指针指向的所有数据块
    if(nd.addrs[NDIRECT] != 0) {
        uint ndirect_ptr;
        lseek(img_file, nd.addrs[NDIRECT] * BSIZE, SEEK_SET); // 定位到间接块
        for(uint i = 0; i < NINDIRECT; ++i) {
            read(img_file, &ndirect_ptr, sizeof(uint)); // 读取指针值
            if(ndirect_ptr != 0 && (ndirect_ptr < bitmap_end || ndirect_ptr > fs_end)) 
                return 1; // 地址超出有效范围
        }
    }
    
    return 0;
}

/**
 * 错误检查3：验证根目录是否存在且正确设置
 * 
 * 算法:
 * 1. 读取根inode(inode 1)的内容
 * 2. 验证根inode的类型是否为目录(T_DIR)
 * 3. 遍历根目录的数据块，查找".."条目
 * 4. 验证".."条目是否存在且指向inode 1(自身)
 * 
 * 判断标准:
 * - 根inode必须是目录类型
 * - 根目录必须包含".."条目
 * - 根目录的".."条目必须指向自身(inode号为1)
 * 
 * 错误影响:
 * 根目录是文件系统的起点，如果根目录不存在或设置错误:
 * 1. 文件系统无法正常挂载
 * 2. 无法从根开始遍历文件系统
 * 3. 可能导致整个文件系统无法访问
 * 
 * @return 如果根目录不存在或设置错误返回1，否则返回0
 */
int error_check_3() {
    struct dinode root_inode;
    lseek(img_file, sblock.inodestart * BSIZE + sizeof(struct dinode), SEEK_SET); // 定位到根inode（inode 1）
    read(img_file, &root_inode, sizeof(struct dinode));

    if(root_inode.type != T_DIR)
        return 1; // 根inode不是目录类型

    struct dirent dir_entry;
    for(uint i = 0; i < NDIRECT; ++i)
        if(root_inode.addrs[i] != 0) {
            lseek(img_file, root_inode.addrs[i] * BSIZE, SEEK_SET); // 定位到目录数据块
            for(uint dir_cnt = 0; dir_cnt < BSIZE / (sizeof(struct dirent)); ++dir_cnt) {
                read(img_file, &dir_entry, sizeof(struct dirent)); // 读取目录项

                if(dir_entry.inum != 0) 
                    if(strncmp(dir_entry.name, "..", DIRSIZ) == 0 && dir_entry.inum == 1)
                        return 0; // 找到正确的".."条目
            }
            
        }

    return 1; // 未找到正确的".."条目
}

/**
 * 错误检查4：检查目录是否正确格式化
 * 
 * 算法:
 * 1. 遍历目录inode的所有直接数据块
 * 2. 对每个数据块，读取并检查所有目录项
 * 3. 查找名为"."和".."的两个特殊条目
 * 4. 验证"."条目指向目录自身
 * 
 * 判断标准:
 * - 每个目录必须恰好有一个"."条目，且指向自身(inum等于目录的inode号)
 * - 每个目录必须恰好有一个".."条目，指向父目录
 * - 两个条目缺一不可，且必须正确设置
 * 
 * 错误影响:
 * 如果目录格式不正确:
 * 1. 可能无法正确遍历文件系统层次结构
 * 2. "."条目错误会导致相对路径解析问题
 * 3. ".."条目错误会导致无法访问上级目录或形成循环引用
 * 
 * @param nd 要检查的目录inode
 * @param inode_num inode号
 * @return 如果目录格式错误返回1，否则返回0
 */
int error_check_4(struct dinode nd, uint inode_num) {
    int chk = 0;
    struct dirent dir_entry;
    for(uint i = 0; i < NDIRECT; ++i) {
        if(nd.addrs[i] != 0) {
            lseek(img_file, nd.addrs[i] * BSIZE, SEEK_SET);
            for(uint dir_cnt = 0; dir_cnt < BSIZE / (sizeof(struct dirent)); ++dir_cnt) {
                read(img_file, &dir_entry, sizeof(struct dirent));

                if(strncmp(dir_entry.name, ".", DIRSIZ) == 0 && dir_entry.inum == inode_num)
                    chk += 1; // 找到"."条目

                if(strncmp(dir_entry.name, "..", DIRSIZ) == 0)
                    chk += 1; // 找到".."条目
            }
        }
    }
    if(chk != 2) // 必须同时找到"."和".."
        return 1;
    return 0;
}

/**
 * 错误检查5：验证inode使用的所有数据块在位图中被标记为已使用
 * 
 * 算法:
 * 1. 对inode的每个直接块和间接块:
 *    a. 计算该块在位图中的字节位置和位偏移
 *    b. 读取相应位图字节
 *    c. 检查该位是否被设置为1(已使用)
 * 2. 如果inode有间接块，读取间接块内容，对每个非零指针重复上述检查
 * 
 * 判断标准:
 * - 对于inode使用的每个非零数据块，位图中对应位必须为1
 * - 位计算方式: 块号除以8确定字节，块号模8确定位偏移
 * - 通过右移和按位与操作检查位值
 * 
 * 错误影响:
 * 如果数据块在位图中标记为空闲但实际被使用:
 * 1. 分配器可能会将该块再次分配给其他文件，导致数据覆盖
 * 2. 可能出现同一块被多个文件使用的情况，造成数据损坏
 * 3. 当其中一个文件删除时可能会错误地释放正在使用的块
 * 
 * @param nd 要检查的inode
 * @return 如果有数据块被标记为空闲返回1，否则返回0
 */
int error_check_5(struct dinode nd) {
    // 检查直接块和间接块指针本身
    for(uint i = 0; i < NDIRECT + 1; ++i) {
        if(nd.addrs[i] != 0) {
            uint usebit, shift_amt;

            lseek(img_file, sblock.bmapstart * BSIZE + nd.addrs[i] / 8, SEEK_SET); // 定位到位图字节
            read(img_file, &usebit, sizeof(uint));

            shift_amt = nd.addrs[i] % 8; // 计算位偏移
            usebit >>= shift_amt;
            if((usebit & 1) == 0) // 检查位是否为1
                return 1; // 块在位图中被标记为空闲
        }
    }

    // 检查间接块指向的所有数据块
    if(nd.addrs[NDIRECT] != 0) {
        uint ndirect_ptr;
        for(uint i = 0; i < NINDIRECT; ++i) {
            lseek(img_file, nd.addrs[NDIRECT] * BSIZE + i * sizeof(uint), SEEK_SET);
            read(img_file, &ndirect_ptr, sizeof(uint));
            if(ndirect_ptr != 0) {
                uint usebit, shift_amt;

                lseek(img_file, sblock.bmapstart * BSIZE + ndirect_ptr / 8, SEEK_SET);
                read(img_file, &usebit, sizeof(uint));

                shift_amt = ndirect_ptr % 8;
                usebit >>= shift_amt;
                if((usebit & 1) == 0)
                    return 1; // 块在位图中被标记为空闲
            }
        }
    }

    return 0;
}

/**
 * 错误检查6：验证位图中标记为使用的块确实被某个inode使用
 * 
 * 算法:
 * 1. 读取文件系统位图的相关部分
 * 2. 对每个位图字节:
 *    a. 解析该字节中的8个位，每位对应一个块
 *    b. 对每个标记为已使用(位值为1)的块，检查in_use数组对应值
 *    c. 如果in_use[块号]为0，表示该块在位图中标记为已使用但实际未找到使用者
 * 
 * 判断标准:
 * - 位图中每个标记为已使用的块，必须能在某个inode的直接块或间接块中找到
 * - 通过in_use数组跟踪实际使用情况，由之前的error_check_7和error_check_8填充
 * 
 * 错误影响:
 * 如果位图中块标记为已使用但实际未被使用:
 * 1. 会导致可用空间计算错误，系统认为磁盘比实际更满
 * 2. 空闲块无法被分配，造成存储空间浪费
 * 3. 长期积累可能导致"磁盘空间不足"的假象
 * 
 * @param in_use 块使用情况数组，由前面的检查函数填充
 * @return 如果有块被标记为使用但实际未使用返回1，否则返回0
 */
int error_check_6(uint* in_use) {
    lseek(img_file, sblock.bmapstart * BSIZE + sblock.size / 8 - sblock.nblocks / 8, SEEK_SET);

    uchar bits;
    uint cnt = sblock.bmapstart + 1;
    for(uint i = cnt; i < sblock.size; i += 8) {
        read(img_file, &bits, sizeof(uchar)); // 读取位图字节

        for(uint offset = 0; offset < 8; ++offset, ++cnt) {
            if((bits >> offset) % 2) { // 检查位图中的位
                if(in_use[cnt] == 0) {
                    return 1; // 块在位图中被标记为使用但实际未使用
                }
            }
        }
    }

    return 0;
}

/**
 * 错误检查7：检查直接块是否被多次使用（存在交叉链接）
 * 
 * 算法:
 * 1. 对inode的每个直接块指针(包括NDIRECT个直接块和1个间接块指针):
 *    a. 检查该块号是否非零
 *    b. 如果非零，检查该块是否已在in_use数组中标记为已使用
 *    c. 如果已被标记，则表示该块被多次使用
 *    d. 否则，将该块在in_use数组中标记为已使用
 * 
 * 判断标准:
 * - 每个数据块应该只被一个inode的一个块指针使用
 * - 如果in_use[块号]已为1，表示该块已被其他inode或同一inode的其他部分使用
 * 
 * 错误影响:
 * 如果直接块被多次使用:
 * 1. 多个文件或目录会共享同一数据块，导致数据混乱
 * 2. 一个文件的写操作会影响另一个文件的内容
 * 3. 删除一个文件可能会导致另一个仍在使用同一块的文件数据丢失
 * 
 * @param nd 要检查的inode
 * @param in_use 块使用情况数组，标记每个块是否已被使用
 * @return 如果有直接块被多次使用返回1，否则返回0
 */
int error_check_7(struct dinode nd, uint *in_use) {
    for(uint i = 0; i < NDIRECT + 1; ++i) {
        if(nd.addrs[i] != 0) {
            if(in_use[nd.addrs[i]])
                return 1; // 块已被使用
            in_use[nd.addrs[i]] = 1; // 标记块为已使用
        }
    }
    return 0;
}

/**
 * 错误检查8：检查间接块指向的块是否被多次使用（存在交叉链接）
 * 
 * 算法:
 * 1. 检查inode是否有间接块(addrs[NDIRECT]非零)
 * 2. 如果有，读取间接块内容
 * 3. 对间接块中的每个块指针:
 *    a. 检查该指针是否非零
 *    b. 如果非零，检查该块是否已在in_use数组中标记为已使用
 *    c. 如果已标记，则表示该块被多次使用
 *    d. 否则，将该块在in_use数组中标记为已使用
 * 
 * 判断标准:
 * - 间接块指向的每个数据块应该只被一次使用
 * - 如果in_use[块号]为1，表示该块已被某个直接块或其他间接块引用
 * 
 * 错误影响:
 * 与直接块重复类似，但影响的可能是文件的扩展部分:
 * 1. 大文件的数据会与其他文件混合
 * 2. 可能导致文件内容不一致
 * 3. 同一块的多次引用会引起释放和修改时的问题
 * 
 * @param nd 要检查的inode
 * @param in_use 块使用情况数组
 * @return 如果有间接块被多次使用返回1，否则返回0
 */
int error_check_8(struct dinode nd, uint *in_use) {
    if(nd.addrs[NDIRECT] != 0) {
        uint ndirect_ptr;
        for(uint i = 0; i < NINDIRECT; ++i) {
            lseek(img_file, nd.addrs[NDIRECT] * BSIZE + i * sizeof(uint), SEEK_SET);
            read(img_file, &ndirect_ptr, sizeof(uint));
            if(ndirect_ptr != 0) {
                if(in_use[ndirect_ptr] == 1)
                    return 1; // 块已被使用
                in_use[ndirect_ptr] = 1; // 标记块为已使用
            }
        }
    }
    return 0;
}

/**
 * 错误检查9：验证标记为使用的inode在某个目录中有条目
 * 
 * 算法:
 * 1. 遍历文件系统中的所有inode
 * 2. 对每个类型为目录且不是待检查inode自身或根目录的inode:
 *    a. 检查其所有直接块和间接块中的目录项
 *    b. 查找是否有目录项引用了待检查的inode号
 * 
 * 判断标准:
 * - 除了根目录外，每个已使用的inode必须至少被一个目录项引用
 * - 如果在所有目录中都找不到引用此inode的条目，则认为是错误
 * 
 * 错误影响:
 * 如果inode被标记为使用但没有目录引用:
 * 1. 形成"孤立inode"，无法通过正常文件系统操作访问
 * 2. 占用系统资源但无法被用户使用或释放
 * 3. 可能是文件系统不一致或目录损坏的表现
 * 
 * 注意:
 * 此函数中存在一个bug: 在第一个for循环内使用了i++而非j++，
 * 会导致外层循环变量被错误递增，可能跳过目录检查。
 * 
 * @param inode_num 要检查的inode号
 * @return 如果inode不在任何目录中返回1，否则返回0
 */
int error_check_9(uint inode_num) {
    struct dinode tmp_inode;
    for(uint i = 0; i < sblock.ninodes; ++i) {
        lseek(img_file, sblock.inodestart * BSIZE + i * sizeof(struct dinode), SEEK_SET);
        read(img_file, &tmp_inode, sizeof(struct dinode));

        if(tmp_inode.type == T_DIR && inode_num != i && i != 1) {
            for(uint j = 0; j < NDIRECT; ++i) { // 注意: 这里有个bug，应该是j++而不是i++
                if(tmp_inode.addrs[j] != 0) {
                    struct dirent dir_entry;

                    lseek(img_file, tmp_inode.addrs[j] * BSIZE, SEEK_SET);
                    read(img_file, &dir_entry, sizeof(struct dirent));

                    if(dir_entry.inum == inode_num)
                        return 0; // 在目录中找到inode引用
                }
            }

            // 检查间接块
            if(tmp_inode.addrs[NDIRECT] != 0) {
                uint addr;
                for(uint j = 0; j < NINDIRECT; ++j) {
                    lseek(img_file, tmp_inode.addrs[NDIRECT] * BSIZE + j * sizeof(uint), SEEK_SET);
                    read(img_file, &addr, sizeof(uint));

                    if(addr != 0) {
                        struct dirent dir_entry;

                        lseek(img_file, addr * BSIZE, SEEK_SET);
                        read(img_file, &dir_entry, sizeof(struct dirent));

                        if(dir_entry.inum == inode_num) 
                            return 0; // 在目录中找到inode引用
                    }
                }
            }

        }
    }
    return 1; // 未在任何目录中找到inode引用
}

/**
 * 错误检查10：验证目录中引用的inode被标记为已使用
 * 
 * 算法:
 * 1. 对目录inode的所有直接块:
 *    a. 读取所有目录项
 *    b. 对每个非零inum的目录项，读取其引用的inode
 *    c. 检查该inode的type是否为非零(已使用)
 * 2. 如果目录有间接块，对其指向的数据块重复上述检查
 * 
 * 判断标准:
 * - 目录中每个有效目录项(inum非零)必须指向一个非空闲的inode
 * - 如果目录项引用的inode的type为0，则表示该inode被错误地标记为空闲
 * 
 * 错误影响:
 * 如果目录引用了空闲inode:
 * 1. 尝试访问该文件会导致系统混乱或崩溃
 * 2. 该空闲inode可能被分配给新文件，造成目录指向错误的数据
 * 3. 表示目录结构与inode分配状态不一致
 * 
 * @param nd 要检查的目录inode
 * @return 如果有引用的inode被标记为空闲返回1，否则返回0
 */
int error_check_10(struct dinode nd) {
    // 检查直接块中的目录条目
    for(uint i = 0; i < NDIRECT; ++i) {
        if(nd.addrs[i] != 0) {
            struct dirent dir_entry;
            for(uint j = 0; j < BSIZE / sizeof(struct dirent); ++j) {
                lseek(img_file, nd.addrs[i] * BSIZE + j * sizeof(struct dirent), SEEK_SET);
                read(img_file, &dir_entry, sizeof(struct dirent));

                struct dinode tmp_nd;
                if(dir_entry.inum != 0) {
                    lseek(img_file, sblock.inodestart * BSIZE + dir_entry.inum * sizeof(struct dinode), SEEK_SET);
                    read(img_file, &tmp_nd, sizeof(struct dinode));

                    if(tmp_nd.type == 0) {
                        return 1; // 引用的inode类型为0（空闲）
                    }
                }
            }
        }
    }

    // 检查间接块中的目录条目
    if(nd.addrs[NDIRECT] != 0) {
        uint addr;
        struct dirent dir_entry;
        for(uint i = 0; i < NINDIRECT; ++i) {
            lseek(img_file, nd.addrs[NDIRECT] * BSIZE + i * sizeof(uint), SEEK_SET);
            read(img_file, &addr, sizeof(uint));

            for(uint j = 0; j < BSIZE / sizeof(struct dirent); ++j) {
                lseek(img_file, addr * BSIZE + j * sizeof(struct dirent), SEEK_SET);
                read(img_file, &dir_entry, sizeof(struct dirent));

                struct dinode tmp_nd;
                if(dir_entry.inum != 0) {
                    lseek(img_file, sblock.inodestart * BSIZE + dir_entry.inum * sizeof(struct dinode), SEEK_SET);
                    read(img_file, &tmp_nd, sizeof(struct dinode));

                    if(tmp_nd.type == 0) {
                        return 1; // 引用的inode类型为0（空闲）
                    }
                }
            }
        }
    }
    return 0;
}

/**
 * 错误检查11：验证文件的引用计数是否正确
 * 
 * 算法:
 * 1. 初始化计数器cnt为0
 * 2. 遍历所有inode寻找目录
 * 3. 对每个目录inode:
 *    a. 检查其所有直接块和间接块中的目录项
 *    b. 计算引用了指定inode_num的目录项数量
 * 4. 与inode自身记录的nlink(硬链接数)值比较
 * 
 * 判断标准:
 * - 文件inode的nlink值必须等于实际在所有目录中引用该inode的目录项数量
 * - 两者不同表示引用计数不正确
 * 
 * 错误影响:
 * 如果引用计数不正确:
 * 1. nlink值过大: 文件可能永远不会被删除，即使删除了所有可见引用
 * 2. nlink值过小: 文件可能在仍有目录引用时被过早删除，导致悬挂引用
 * 3. 影响文件系统的资源回收和文件删除机制
 * 
 * @param nd 要检查的文件inode
 * @param inode_num inode号
 * @return 如果引用计数不正确返回1，否则返回0
 */
int error_check_11(struct dinode nd, uint inode_num) {
    uint cnt = 0; // 引用计数
    struct dinode tmp_inode;
    
    // 遍历所有inode寻找目录
    for(uint i = 0; i < sblock.ninodes; ++i) {
        lseek(img_file, sblock.inodestart * BSIZE + i * sizeof(struct dinode), SEEK_SET);
        read(img_file, &tmp_inode, sizeof(struct dinode));

        if(tmp_inode.type == T_DIR) {
            // 检查直接块中的目录条目
            for(uint j = 0; j < NDIRECT; ++j) {
                if(tmp_inode.addrs[j] != 0) {
                    struct dirent dir_entry;
                    for(uint dir_cnt = 0; dir_cnt < BSIZE / sizeof(struct dirent); ++dir_cnt) {
                        lseek(img_file, tmp_inode.addrs[j] * BSIZE + dir_cnt * sizeof(struct dirent), SEEK_SET);
                        read(img_file, &dir_entry, sizeof(struct dirent));

                        if(dir_entry.inum == inode_num)
                            cnt++; // 找到一个引用，增加计数
                    }
                }
            }

            // 检查间接块中的目录条目
            if(tmp_inode.addrs[NDIRECT] != 0) {
                uint addr;
                for(uint j = 0; j < NINDIRECT; ++j) {
                    lseek(img_file, tmp_inode.addrs[NDIRECT] * BSIZE + j * sizeof(uint), SEEK_SET);
                    read(img_file, &addr, sizeof(uint));

                    if(addr != 0) {
                        struct dirent dir_entry;
                        for(uint dir_cnt = 0; dir_cnt < BSIZE / sizeof(struct dirent); ++dir_cnt) {
                            lseek(img_file, addr * BSIZE + dir_cnt * sizeof(struct dirent), SEEK_SET);
                            read(img_file, &dir_entry, sizeof(struct dirent));

                            if(dir_entry.inum == inode_num)
                                cnt++; // 找到一个引用，增加计数
                        }
                    }
                }
            }
        }
    }

    if(cnt != nd.nlink)
        return 1; // 引用计数不匹配

    return 0;
}

/**
 * 错误检查12：验证每个目录只出现一次
 * 
 * 算法:
 * 1. 初始化计数器cnt为0
 * 2. 遍历所有inode寻找目录
 * 3. 对每个目录inode:
 *    a. 检查其所有直接块和间接块中的目录项
 *    b. 计算引用了指定inode_num的目录项数量
 * 4. 检查计数值是否为1
 * 
 * 判断标准:
 * - 每个目录(除根目录外)必须只有一个父目录引用它
 * - 如果计数不为1，表示目录要么没有父目录，要么有多个父目录
 * 
 * 错误影响:
 * 如果目录在多处出现:
 * 1. 破坏文件系统的树状结构，形成有环图
 * 2. 可能导致遍历算法无限循环
 * 3. 删除操作可能不完全，留下悬挂引用
 * 4. 父目录(..)解析可能不一致
 * 
 * @param nd 要检查的目录inode
 * @param inode_num inode号
 * @return 如果目录在多处出现返回1，否则返回0
 */
int error_check_12(struct dinode nd, uint inode_num) {
    uint cnt = 0; // 引用计数
    struct dinode tmp_inode;
    
    // 遍历所有inode寻找目录
    for(uint i = 0; i < sblock.ninodes; ++i) {
        lseek(img_file, sblock.inodestart * BSIZE + i * sizeof(struct dinode), SEEK_SET);
        read(img_file, &tmp_inode, sizeof(struct dinode));

        if(tmp_inode.type == T_DIR) {
            // 检查直接块中的目录条目
            for(uint j = 0; j < NDIRECT; ++j) {
                if(tmp_inode.addrs[j] != 0) {
                    struct dirent dir_entry;
                    for(uint dir_cnt = 0; dir_cnt < BSIZE / sizeof(struct dirent); ++dir_cnt) {
                        lseek(img_file, tmp_inode.addrs[j] * BSIZE + dir_cnt * sizeof(struct dirent), SEEK_SET);
                        read(img_file, &dir_entry, sizeof(struct dirent));

                        if(dir_entry.inum == inode_num)
                            cnt++; // 找到一个引用，增加计数
                    }
                }
            }

            // 检查间接块中的目录条目
            if(tmp_inode.addrs[NDIRECT] != 0) {
                uint addr;
                for(uint j = 0; j < NINDIRECT; ++j) {
                    lseek(img_file, tmp_inode.addrs[NDIRECT] * BSIZE + j * sizeof(uint), SEEK_SET);
                    read(img_file, &addr, sizeof(uint));

                    if(addr != 0) {
                        struct dirent dir_entry;
                        for(uint dir_cnt = 0; dir_cnt < BSIZE / sizeof(struct dirent); ++dir_cnt) {
                            lseek(img_file, addr * BSIZE + dir_cnt * sizeof(struct dirent), SEEK_SET);
                            read(img_file, &dir_entry, sizeof(struct dirent));

                            if(dir_entry.inum == inode_num)
                                cnt++; // 找到一个引用，增加计数
                        }
                    }
                }
            }
        }
    }

    if(cnt != 1) // 目录应该只有一个引用（除了根目录外）
        return 1;

    return 0;
}

/**
 * 主函数 - 文件系统检查的入口点
 */
int main(int argc, char* argv[]) {
    // 检查命令行参数
    if(argc != 2) {
        fprintf(stderr, "Usage: xcheck <file_system_image>\n");
        return 1;
    }

    // 打开文件系统镜像
    img_file = open(argv[1], O_RDONLY);
    if(img_file < 0) {
        fprintf(stderr, "image not found\n");
        return 1;
    }

    // 加载超级块
    lseek(img_file, BSIZE * 1, SEEK_SET);
    read(img_file, &sblock, sizeof(sblock));

    // 初始化块使用数组
    uint in_use[sblock.size];
    for(int i = 0; i < sblock.size; ++i)
        in_use[i] = 0;

    // 首先检查根目录是否存在
    if(error_check_3()) {
        fprintf(stderr, "ERROR: root directory does not exist\n");
        close(img_file);
        exit(1);
    }

    // 遍历所有inode进行检查
    for(uint inode_num = 0; inode_num < sblock.ninodes; ++inode_num) {
        // 加载当前inode
        lseek(img_file, sblock.inodestart * BSIZE + sizeof(struct dinode) * inode_num, SEEK_SET);
        read(img_file, &cur_inode, sizeof(struct dinode));

        // 执行各种一致性检查
        if(error_check_1(cur_inode)) {
            fprintf(stderr, "ERROR: bad inode\n");
            close(img_file);
            exit(1);
        }

        // 只检查非空闲的inode
        if(cur_inode.type != 0) {
            if(error_check_2(cur_inode)) {
                fprintf(stderr, "ERROR: bad indirect address in inode\n");
                close(img_file);
                exit(1);
            }

            if(error_check_5(cur_inode)) {
                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap\n");
                close(img_file);
                exit(1);
            }

            if(error_check_7(cur_inode, in_use)) {
                fprintf(stderr, "ERROR: direct address used more than once\n");
                close(img_file);
                exit(1);
            }

            if(error_check_8(cur_inode, in_use)) {
                fprintf(stderr, "ERROR: indirect address used more than once\n");
                close(img_file);
                exit(1);
            }

            // 目录特有的检查
            if(cur_inode.type == T_DIR) {
                if(error_check_4(cur_inode, inode_num)) {
                    fprintf(stderr, "ERROR: directory not properly formatted\n");
                    close(img_file);
                    exit(1);
                }
                
                if(inode_num != 1){ // 根目录除外
                    if(error_check_9(inode_num)) {
                        fprintf(stderr, "ERROR: inode marked use but not found in a directory\n");
                        close(img_file);
                        exit(1);
                    }

                    if(error_check_12(cur_inode, inode_num)) {
                        fprintf(stderr, "ERROR: directory appears more than once in file system\n");
                        close(img_file);
                        exit(1);
                    }
                }

                if(error_check_10(cur_inode)) {
                    fprintf(stderr, "ERROR: inode referred to in directory but marked free\n");
                    close(img_file);
                    exit(1);
                }
            }

            // 文件特有的检查
            if(cur_inode.type == T_FILE) {
                if(error_check_11(cur_inode, inode_num)) {
                    fprintf(stderr, "ERROR: bad reference count for file\n");
                    close(img_file);
                    exit(1);
                }
            }
        }
    }

    // 检查位图一致性
    if(error_check_6(in_use)) {
        fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use\n");
        close(img_file);
        exit(1);
    }

    return 0; // 所有检查通过
}