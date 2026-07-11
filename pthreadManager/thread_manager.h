#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

typedef struct thread_pool thread_pool_t;

thread_pool_t* thread_pool_create(int num_threads);
void           thread_pool_destroy(thread_pool_t* pool);

int thread_pool_submit(thread_pool_t* pool,
                       void* (*func)(void*), void* arg);

void thread_pool_wait_all(thread_pool_t* pool);

void thread_pool_set_cpu_ratio(double ratio);
int  thread_pool_optimal_count(void);

int thread_pool_queue_size(thread_pool_t* pool);
int thread_pool_active_count(thread_pool_t* pool);

int thread_pool_execute(void* (*func)(void*), void* args[],
                        int num_tasks, int num_threads);

#endif
