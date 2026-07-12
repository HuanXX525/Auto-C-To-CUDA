#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

/**
 * @brief 任务参数结构体
 *
 * 每个任务实例对应一组 (argc, argv) 参数，
 * 由 parse_args 从 JSON 文件中解析构建。
 */
typedef struct {
    int argc;      /**< 参数个数（含 argv[0] 程序名） */
    char** argv;   /**< 参数字符串数组，末尾 NULL 结尾 */
} task_param_t;

/** @brief 线程池不透明类型 */
typedef struct thread_pool thread_pool_t;

/**
 * @brief 创建线程池
 * @param num_threads 工作线程数，≤0 则自动计算最优值
 * @return 线程池指针，失败返回 NULL
 */
thread_pool_t* thread_pool_create(int num_threads);

/**
 * @brief 销毁线程池
 *
 * 等待所有任务完成，通知线程退出，回收所有资源。
 * @param pool 线程池指针
 */
void           thread_pool_destroy(thread_pool_t* pool);

/**
 * @brief 提交一个任务到线程池
 * @param pool  线程池指针
 * @param func  任务函数（签名 int func(int argc, char** argv)）
 * @param id    任务编号（用于结果数组索引）
 * @param argc  参数个数
 * @param argv  参数数组（线程池内部不释放，由 func 自行管理）
 * @return 成功 0，失败 -1
 */
int thread_pool_submit(thread_pool_t* pool,
                       int (*func)(int, char**), int id, int argc, char** argv);

/**
 * @brief 阻塞等待所有已提交任务执行完毕
 * @param pool 线程池指针
 */
void thread_pool_wait_all(thread_pool_t* pool);

/**
 * @brief 设置任务的 CPU 使用率预估（影响最优线程数计算）
 * @param ratio 0.01 ~ 1.0，默认 1.0（纯计算型）
 */
void thread_pool_set_cpu_ratio(double ratio);

/**
 * @brief 根据 CPU 核心数和 CPU 使用率计算最优线程数
 * @return 推荐的工作线程数量（≥1）
 */
int  thread_pool_optimal_count(void);

/**
 * @brief 获取队列中尚未执行的任务数
 * @param pool 线程池指针
 * @return 排队任务数
 */
int thread_pool_queue_size(thread_pool_t* pool);

/**
 * @brief 获取当前正在执行的任务数
 * @param pool 线程池指针
 * @return 活跃任务数
 */
int thread_pool_active_count(thread_pool_t* pool);

/**
 * @brief 批量执行任务（快捷接口）
 *
 * 内部完成创建线程池 → 提交所有任务 → 等待完成 → 销毁的完整流程。
 *
 * @param func        任务函数指针
 * @param args        任务参数数组，每个元素对应一个任务
 * @param num_tasks   任务数量
 * @param num_threads 线程数，0 表示自动
 * @return int* 每个任务的返回值数组（长度 num_tasks），
 *         调用者须 free。失败返回 NULL。
 */
int* thread_pool_execute(int (*func)(int, char**), task_param_t* args[],
                         int num_tasks, int num_threads);

/**
 * @brief 高级入口：解析 JSON → 执行任务 → 输出总结
 *
 * 完整流程：
 *   1. 初始化日志（控制台或文件）
 *   2. 解析 JSON 参数文件
 *   3. 多线程执行所有任务
 *   4. 输出每个任务的返回值
 *   5. 打印执行时间总结
 *   6. 释放所有资源
 *
 * @param func      任务函数，须符合 int (*)(int, char**) 签名
 * @param json_path 任务参数字典 JSON 文件路径
 * @param log_path  日志路径，传 NULL 则输出到控制台
 * @return 成功 0，失败 -1
 */
int thread_pool_run(int (*func)(int, char**), const char* json_path, const char* log_path);

#endif
