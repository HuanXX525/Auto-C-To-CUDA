#include "thread_manager.h"
#include "log.h"
#include "cJSON.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * @brief 任务节点（内部链表节点）
 *
 * 每个节点封装一个待执行的任务，包含函数指针和参数，
 * 通过 next 指针串联成 FIFO 队列。
 */
typedef struct task_node {
    struct task_node* next;            /**< 链表下一节点 */
    int (*func)(int, char**);          /**< 任务函数 */
    int id;                            /**< 任务编号 */
    int argc;                          /**< 参数个数 */
    char** argv;                       /**< 参数数组 */
} task_node_t;

/**
 * @brief 线程池结构体（内部实现）
 *
 * 管理一组工作线程、一个互斥保护的任务队列，
 * 以及用于线程同步的条件变量。
 */
struct thread_pool {
    pthread_t* workers;         /**< 工作线程数组 */
    int num_threads;            /**< 工作线程数量 */

    task_node_t* queue_head;    /**< 任务队列头 */
    task_node_t* queue_tail;    /**< 任务队列尾 */
    int queue_size;             /**< 队列长度 */

    pthread_mutex_t mutex;      /**< 队列与状态互斥锁 */
    pthread_cond_t  cond;       /**< 有新任务时唤醒工作线程 */
    pthread_cond_t  complete;   /**< 所有任务完成时通知等待者 */

    volatile int active_count;  /**< 正在执行的任务数 */
    volatile int shutdown;      /**< 销毁标志，1 表示正在关闭 */

    int* results;               /**< 任务返回值数组 */
    int num_results;            /**< results 数组长度 */
};

static double g_cpu_ratio = 1.0;

/**
 * @brief 获取 CPU 核心数
 *
 * Windows 下调用 GetSystemInfo，其他平台调用 sysconf。
 * @return CPU 逻辑核心数，至少返回 1
 */
static int get_cpu_cores(void) {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (int)sysinfo.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
#endif
}

/**
 * @brief 根据 CPU 核心数和 CPU 使用率计算最优线程数
 *
 * 公式：threads = cores × (1 + (1 - ratio) / ratio)
 * 纯计算任务 ratio=1 → threads = cores
 * IO 密集型 ratio 较小 → 自动增大线程数
 *
 * @return 推荐线程数（≥1）
 */
int thread_pool_optimal_count(void) {
    int cores = get_cpu_cores();
    double factor = 1.0 + (1.0 - g_cpu_ratio) / g_cpu_ratio;
    int n = (int)(cores * factor);
    return n < 1 ? 1 : n;
}

/**
 * @brief 设置任务的 CPU 使用率预估值
 *
 * 影响 thread_pool_optimal_count 的计算结果。
 * 值域被裁剪到 [0.01, 1.0]。
 *
 * @param ratio 0.01（纯 IO）~ 1.0（纯计算）
 */
void thread_pool_set_cpu_ratio(double ratio) {
    if (ratio < 0.01) ratio = 0.01;
    if (ratio > 1.0)  ratio = 1.0;
    g_cpu_ratio = ratio;
}

/**
 * @brief 将任务节点插入队列尾部
 *
 * 线程不安全，调用者须持有 pool->mutex 锁。
 *
 * @param pool 线程池指针
 * @param node 待插入的任务节点
 */
static void enqueue(struct thread_pool* pool, task_node_t* node) {
    node->next = NULL;
    if (pool->queue_tail) {
        pool->queue_tail->next = node;
    } else {
        pool->queue_head = node;
    }
    pool->queue_tail = node;
    pool->queue_size++;
}

/**
 * @brief 从队列头部取出一个任务节点
 *
 * 线程不安全，调用者须持有 pool->mutex 锁。
 *
 * @param pool 线程池指针
 * @return 队首节点，队列为空返回 NULL
 */
static task_node_t* dequeue(struct thread_pool* pool) {
    task_node_t* node = pool->queue_head;
    if (!node) return NULL;
    pool->queue_head = node->next;
    if (!pool->queue_head) pool->queue_tail = NULL;
    pool->queue_size--;
    return node;
}

/**
 * @brief 工作线程主循环
 *
 * 每个工作线程独立运行此函数：
 *   1. 加锁检查队列，若无任务则等待条件变量 cond
 *   2. 唤醒后取出队首任务，增加 active_count 后解锁
 *   3. 执行任务函数，将返回值写入 results[task->id]
 *   4. 回收任务节点，减少 active_count
 *   5. 若全部任务完成，发出 complete 信号
 *
 * @param arg 线程池指针（struct thread_pool*）
 * @return 始终返回 NULL
 */
static void* worker_wrapper(void* arg) {
    struct thread_pool* pool = (struct thread_pool*)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);
        while (!pool->queue_head && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }
        if (pool->shutdown && !pool->queue_head) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        task_node_t* task = dequeue(pool);
        pool->active_count++;
        pthread_mutex_unlock(&pool->mutex);

        LOG_TRACE("[Task %d] start", task->id);
        clock_t t0 = clock();
        int ret = task->func(task->argc, task->argv);
        if (task->id < pool->num_results)
            pool->results[task->id] = ret;
        clock_t t1 = clock();
        double ms = (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0;
        LOG_TRACE("    task done in %.3f ms", ms);

        pthread_mutex_lock(&pool->mutex);
        pool->active_count--;
        free(task);
        if (pool->active_count == 0 && pool->queue_size == 0) {
            pthread_cond_signal(&pool->complete);
        }
        pthread_mutex_unlock(&pool->mutex);
    }
    return NULL;
}

/**
 * @brief 创建线程池
 *
 * 初始化互斥锁和条件变量，分配 workers 数组，
 * 逐一创建 pthread 工作线程。
 * 若任一 pthread_create 失败，已创建的线程会被 join 后统一回收。
 *
 * @param num_threads 工作线程数；≤0 时通过 thread_pool_optimal_count() 自动计算
 * @return 线程池指针，任何失败返回 NULL
 */
thread_pool_t* thread_pool_create(int num_threads) {
    if (num_threads <= 0)
        num_threads = thread_pool_optimal_count();
    if (num_threads < 1) num_threads = 1;

    thread_pool_t* pool = (thread_pool_t*)malloc(sizeof(thread_pool_t));
    if (!pool) return NULL;

    pool->num_threads = num_threads;
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->queue_size = 0;
    pool->active_count = 0;
    pool->shutdown = 0;

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);
    pthread_cond_init(&pool->complete, NULL);

    pool->workers = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    if (!pool->workers) {
        pthread_mutex_destroy(&pool->mutex);
        pthread_cond_destroy(&pool->cond);
        pthread_cond_destroy(&pool->complete);
        free(pool);
        return NULL;
    }

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->workers[i], NULL, worker_wrapper, pool) != 0) {
            pthread_mutex_lock(&pool->mutex);
            pool->shutdown = 1;
            pthread_cond_broadcast(&pool->cond);
            pthread_mutex_unlock(&pool->mutex);
            for (int j = 0; j < i; j++)
                pthread_join(pool->workers[j], NULL);
            pthread_mutex_destroy(&pool->mutex);
            pthread_cond_destroy(&pool->cond);
            pthread_cond_destroy(&pool->complete);
            free(pool->workers);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

/**
 * @brief 提交一个任务到线程池
 *
 * 分配 task_node_t 节点并初始化，线程安全地入队后
 * 通过条件变量 cond 唤醒一个等待中的工作线程。
 *
 * @param pool  线程池指针
 * @param func  任务函数指针
 * @param id    任务编号
 * @param argc  参数个数
 * @param argv  参数数组（由任务函数负责释放）
 * @return 成功 0，参数非法或内存分配失败返回 -1
 */
int thread_pool_submit(thread_pool_t* pool,
                       int (*func)(int, char**), int id, int argc, char** argv)
{
    if (!pool || !func) return -1;

    task_node_t* task = (task_node_t*)malloc(sizeof(task_node_t));
    if (!task) return -1;

    task->func = func;
    task->id = id;
    task->argc = argc;
    task->argv = argv;

    pthread_mutex_lock(&pool->mutex);
    enqueue(pool, task);
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

/**
 * @brief 阻塞等待所有已提交任务执行完毕
 *
 * 条件等待 complete 信号。worker_wrapper 在
 * active_count == 0 && queue_size == 0 时触发该信号。
 *
 * @param pool 线程池指针
 */
void thread_pool_wait_all(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->mutex);
    while (pool->queue_size > 0 || pool->active_count > 0) {
        pthread_cond_wait(&pool->complete, &pool->mutex);
    }
    pthread_mutex_unlock(&pool->mutex);
}

/**
 * @brief 销毁线程池
 *
 * 步骤：
 *   1. 等待所有已提交任务完成
 *   2. 设置 shutdown 标志，广播唤醒所有工作线程
 *   3. join 所有工作线程
 *   4. 清空残留队列节点
 *   5. 销毁互斥锁 / 条件变量，释放内存
 *
 * @param pool 线程池指针，NULL 安全
 */
void thread_pool_destroy(thread_pool_t* pool) {
    if (!pool) return;

    thread_pool_wait_all(pool);

    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->num_threads; i++)
        pthread_join(pool->workers[i], NULL);

    while (pool->queue_head) {
        task_node_t* tmp = pool->queue_head;
        pool->queue_head = tmp->next;
        free(tmp);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    pthread_cond_destroy(&pool->complete);
    free(pool->workers);
    free(pool);
}

/**
 * @brief 批量执行任务（快捷接口）
 *
 * 简化流程：创建线程池 → 提交所有任务 → 等待完成 → 销毁。
 * 内部自动分配 results 数组用于收集每个任务的返回值。
 * 销毁线程池后返回 results，results 仍可安全读取。
 *
 * @param func        任务函数
 * @param args        任务参数数组，args[i] 对应第 i 个任务
 * @param num_tasks   任务总数
 * @param num_threads 线程数，传 0 自动计算
 * @return int* 长度为 num_tasks 的返回值数组，
 *         调用者须用 free() 释放。失败返回 NULL。
 */
int* thread_pool_execute(int (*func)(int, char**), task_param_t* args[],
                         int num_tasks, int num_threads)
{
    if (!func || !args || num_tasks <= 0) return NULL;

    thread_pool_t* pool = thread_pool_create(num_threads);
    if (!pool) return NULL;

    int* results = calloc(num_tasks, sizeof(int));
    if (!results) {
        thread_pool_destroy(pool);
        return NULL;
    }
    pool->results = results;
    pool->num_results = num_tasks;

    for (int i = 0; i < num_tasks; i++) {
        if (thread_pool_submit(pool, func, i, args[i]->argc, args[i]->argv) != 0) {
            free(results);
            thread_pool_destroy(pool);
            return NULL;
        }
    }

    thread_pool_wait_all(pool);
    thread_pool_destroy(pool);
    return results;
}

/**
 * @brief 获取当前队列中尚未执行的任务数
 *
 * @param pool 线程池指针
 * @return 排队中的任务数量
 */
int thread_pool_queue_size(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->mutex);
    int n = pool->queue_size;
    pthread_mutex_unlock(&pool->mutex);
    return n;
}

/**
 * @brief 获取当前正在执行的任务数
 *
 * @param pool 线程池指针
 * @return 正在执行的任务数量
 */
int thread_pool_active_count(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->mutex);
    int n = pool->active_count;
    pthread_mutex_unlock(&pool->mutex);
    return n;
}

/**
 * @brief 读取文件全部内容到堆内存字符串
 *
 * 以二进制只读方式打开文件，获取长度后分配缓冲区并读取全部内容。
 * 末尾自动添加 '\0'。
 *
 * @param path 文件路径
 * @return char* 堆分配的字符串，调用者须 free。失败返回 NULL
 */
static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("cannot open '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char* buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t r = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[r] = '\0';
    return buf;
}

/**
 * @brief 解析 JSON 文件并构建任务参数数组
 *
 * 文件格式要求：
 *   [
 *     ["-i", "in.txt", "-o", "out.txt"],
 *     ["-i", "in2.txt", "-o", "out2.txt"]
 *   ]
 * 每个内层数组对应一个任务，会自动添加 argv[0] = "./program"。
 *
 * @param path      JSON 文件路径
 * @param out_count [输出] 任务数量
 * @return task_param_t** 任务参数数组，长度为 *out_count，
 *         每个元素须分别 free(argv) 和 free(p)。失败返回 NULL。
 */
static task_param_t** parse_args(const char* path, int* out_count) {
    char* json_str = read_file(path);
    if (!json_str) return NULL;

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        LOG_ERROR("JSON parse error: %s", cJSON_GetErrorPtr());
        return NULL;
    }

    if (!cJSON_IsArray(root)) {
        LOG_ERROR("%s must be an array", path);
        cJSON_Delete(root);
        return NULL;
    }

    int task_count = cJSON_GetArraySize(root);
    if (task_count <= 0) {
        LOG_ERROR("%s is empty", path);
        cJSON_Delete(root);
        return NULL;
    }

    task_param_t** args = malloc(sizeof(task_param_t*) * task_count);
    if (!args) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < task_count; i++) {
        cJSON* inner = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsArray(inner)) {
            LOG_ERROR("entry %d is not an array", i);
            for (int k = 0; k < i; k++) {
                free(args[k]->argv);
                free(args[k]);
            }
            free(args);
            cJSON_Delete(root);
            return NULL;
        }

        int inner_len = cJSON_GetArraySize(inner);
        task_param_t* p = malloc(sizeof(task_param_t));
        p->argc = inner_len + 1;
        p->argv = malloc(sizeof(char*) * (p->argc + 1));

        p->argv[0] = strdup("./program");
        for (int j = 0; j < inner_len; j++) {
            cJSON* item = cJSON_GetArrayItem(inner, j);
            if (!cJSON_IsString(item)) {
                LOG_ERROR("entry %d element %d is not a string", i, j);
                p->argv[j + 1] = strdup("");
            } else {
                p->argv[j + 1] = strdup(item->valuestring);
            }
        }
        p->argv[p->argc] = NULL;

        args[i] = p;
    }

    cJSON_Delete(root);
    *out_count = task_count;
    return args;
}

/**
 * @brief 高级入口：解析 JSON → 执行任务 → 输出总结
 *
 * 完整流程：
 *   1. 初始化日志（控制台或文件）
 *   2. 调用 parse_args 解析 JSON 参数文件
 *   3. 调用 thread_pool_execute 多线程执行所有任务
 *   4. 逐条输出每个任务的返回值
 *   5. 打印执行时间总结
 *   6. 关闭日志，释放所有资源
 *
 * @param func      任务函数，须符合 int (*)(int, char**) 签名
 * @param json_path 任务参数字典 JSON 文件路径
 * @param log_path  日志路径，传 NULL 则输出到控制台
 * @return 成功 0，失败 -1
 */
int thread_pool_run(int (*func)(int, char**), const char* json_path, const char* log_path) {
    log_open(log_path, log_path ? LOG_INFO : LOG_TRACE);

    int task_count;
    task_param_t** args = parse_args(json_path, &task_count);
    if (!args) {
        log_close();
        return -1;
    }

    LOG_INFO("tasks: %d", task_count);

    clock_t start = clock();
    LOG_INFO("executing...");
    int* results = thread_pool_execute(func, args, task_count, 0);
    LOG_INFO("all done");

    for (int i = 0; i < task_count; i++) {
        LOG_INFO("result[%d] = %d", i, results[i]);
        free(args[i]);
    }
    free(results);
    free(args);

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    LOG_INFO("--- summary ---");
    LOG_INFO("tasks  %d", task_count);
    LOG_INFO("time   %.3f s", elapsed);
    LOG_INFO("avg    %.3f ms/task", elapsed / task_count * 1000.0);

    log_close();
    return 0;
}
