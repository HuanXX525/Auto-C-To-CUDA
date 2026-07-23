#include "thread_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int __Pm__main_task_01(int argc, char **argv);

typedef enum {
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_BOOL,
} param_type_t;

typedef struct {
    const char* long_opt;
    const char* short_opt;
    param_type_t type;
    void*       target;
    const char* desc;
} param_reg_t;

static int parse_params(param_reg_t table[], int n, int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        int matched = 0;
        for (int j = 0; j < n; j++) {
            if (strcmp(argv[i], table[j].long_opt) == 0 ||
                (table[j].short_opt && strcmp(argv[i], table[j].short_opt) == 0))
            {
                matched = 1;
                switch (table[j].type) {
                    case TYPE_BOOL:
                        *(int*)table[j].target = 1;
                        break;
                    case TYPE_INT:
                        if (++i < argc) *(int*)table[j].target = atoi(argv[i]);
                        break;
                    case TYPE_DOUBLE:
                        if (++i < argc) *(double*)table[j].target = atof(argv[i]);
                        break;
                    case TYPE_STRING:
                        if (++i < argc) *(const char**)table[j].target = argv[i];
                        break;
                }
                break;
            }
        }
        if (!matched) {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

static void print_usage(const char* prog, param_reg_t table[], int n) {
    fprintf(stderr, "usage: %s [options]\n\noptions:\n", prog);
    for (int i = 0; i < n; i++) {
        if (table[i].short_opt)
            fprintf(stderr, "  %s, %s", table[i].long_opt, table[i].short_opt);
        else
            fprintf(stderr, "  %s", table[i].long_opt);
        fprintf(stderr, "  %s\n", table[i].desc);
    }
}

int main(int argc, char* argv[])
{
    /* 注册参数 */
    param_reg_t param_table[] = {
        {"--threads",    "-t", TYPE_INT,    &__Pm__g_config.num_threads, "worker thread count (0 = auto)"},
        {"--cpu-ratio",  "-c", TYPE_DOUBLE, &__Pm__g_config.cpu_ratio,   "CPU usage ratio 0.01~1.0"},
        {"--json",       "-j", TYPE_STRING, &__Pm__g_config.json_path,   "task parameter JSON file"},
        {"--log",        "-l", TYPE_STRING, &__Pm__g_config.log_path,    "log file path"},
        {"--help",       "-h", TYPE_BOOL,   &__Pm__g_config.help,        "show this help"},
    };
    int n = sizeof(param_table) / sizeof(param_table[0]);
    /* 解析参数 */
    if (parse_params(param_table, n, argc, argv) != 0)
        return 1;
    /* 帮助 */
    if (__Pm__g_config.help) {
        print_usage(argv[0], param_table, n);
        return 0;
    }
    /* 运行 */
    __Pm__thread_pool_run(__Pm__main_task_01);
    return 0;
}