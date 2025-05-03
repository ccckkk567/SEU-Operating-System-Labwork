/**
 * 行翻转程序 - 读取文本内容并按行反转顺序输出
 * 
 * 使用方法:
 *   reverse            - 从标准输入读取，输出到标准输出
 *   reverse <input>    - 从input文件读取，输出到标准输出
 *   reverse <input> <output> - 从input文件读取，输出到output文件
 */
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>  // 用于获取文件信息
#include <stdlib.h>
#include <string.h>

/**
 * 将文本内容按行翻转顺序
 * 
 * @param text 输入文本数组，每个元素为一行字符串
 * @param line_count 文本的总行数
 * @return 翻转后的文本数组
 */
char** reverse_text(char** text, int line_count) {
    // 为翻转后的文本分配内存
    char** ret = ( char** )malloc(sizeof(char*) * line_count);
    if(!ret) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }
    // 翻转文本行的顺序
    for (int i = 0; i < line_count; ++i) {
        ret[i] = text[line_count - i - 1];
    }
    return ret;
}

/**
 * 将文本内容输出到指定文件
 * 
 * @param file 输出文件指针
 * @param text 要输出的文本数组
 * @param line_count 文本的总行数
 */
void get_output(FILE* file, char** text, int line_count) {
    for (int i = 0; i < line_count; ++i) {
        fprintf(file, "%s\n", text[i]);
    }
}

/**
 * 从输入文件读取文本内容
 * 
 * @param file 输入文件指针
 * @param line_cnt 指向保存行数的变量的指针，用于返回实际读取的行数
 * @return 读取的文本数组
 */
char** get_input(FILE* file, int* line_cnt) {
    // 初始分配100行的内存空间
    char** text = ( char** )malloc(sizeof(char*) * 100);
    if(!text) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }
    // 为每行分配内存
    for (int i = 0; i < 100; ++i) {
        text[i] = ( char* )malloc(sizeof(char) * 100);
        if(!text[i]) {
            fprintf(stderr, "malloc failed\n");
            exit(1);
        }
    }
    // 读取文件内容
    int line_count = 0;
    for (; fscanf(file, "%s", text[line_count]) != -1; ++line_count)
        ;
    // 通过指针返回行数
    *line_cnt = line_count;
    return text;
}

int main(int argc, char* argv[]) {
    // 初始化输入和输出文件路径
    char* input_file_path  = NULL;
    char* output_file_path = NULL;

    // 解析命令行参数
    if (argc == 2) {  // 格式: reverse <input>
        input_file_path = argv[1];
    }
    if (argc == 3) {  // 格式: reverse <input> <output>
        input_file_path  = argv[1];
        output_file_path = argv[2];
    }
    if (argc > 3) {  // 参数过多，显示用法并退出
        fprintf(stderr, "usage: reverse <input> <output>\n");
        exit(1);
    }

    // 默认输入输出为标准输入输出
    FILE* input_file  = stdin;
    FILE* output_file = stdout;

    // 如果指定了输入文件，打开它
    if (input_file_path != NULL) {
        input_file = fopen(input_file_path, "r");
        if (input_file == NULL) {
            fprintf(stderr, "reverse: cannot open file '%s'\n", input_file_path);
            exit(1);
        }
    }
    // 如果指定了输出文件，打开它
    if (output_file_path != NULL) {
        output_file = fopen(output_file_path, "w");
        if (output_file == NULL) {
            fprintf(stderr, "reverse: cannot open file '%s'\n", output_file_path);
            exit(1);
        }
    }

    // 检查输入和输出文件是否为同一文件
    if (input_file_path != NULL && output_file_path != NULL) {
        struct stat input_file_stat, output_file_stat;
        // 获取文件信息
        stat(input_file_path, &input_file_stat);
        stat(output_file_path, &output_file_stat);

        // 比较inode号，判断是否为同一文件
        if(input_file_stat.st_ino == output_file_stat.st_ino) {
            fprintf(stderr, "reverse: input and output file must differ\n");
            exit(1);
        }
    }

    // 读取文本的行数
    int cnt = 0;

    // 读取输入内容
    char** test_text = get_input(input_file, &cnt);
    // 如果没有读取到内容，退出程序
    if (cnt == 0)
        exit(1);
    // 翻转文本并输出
    get_output(output_file, reverse_text(test_text, cnt), cnt);
}