#include "thread_manager.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct task_node {
    struct task_node* next;
    void* (*func)(void*);
    void* arg;
} task_node_t;

struct thread_pool {
    pthread_t* workers;
    int num_threads;

    task_node_t* queue_head;
    task_node_t* queue_tail;
    int queue_size;

    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    pthread_cond_t  complete;

    volatile int active_count;
    volatile int shutdown;
};

static double g_cpu_ratio = 1.0;

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

int thread_pool_optimal_count(void) {
    int cores = get_cpu_cores();
    double factor = 1.0 + (1.0 - g_cpu_ratio) / g_cpu_ratio;
    int n = (int)(cores * factor);
    return n < 1 ? 1 : n;
}

void thread_pool_set_cpu_ratio(double ratio) {
    if (ratio < 0.01) ratio = 0.01;
    if (ratio > 1.0)  ratio = 1.0;
    g_cpu_ratio = ratio;
}

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

static task_node_t* dequeue(struct thread_pool* pool) {
    task_node_t* node = pool->queue_head;
    if (!node) return NULL;
    pool->queue_head = node->next;
    if (!pool->queue_head) pool->queue_tail = NULL;
    pool->queue_size--;
    return node;
}

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

        task->func(task->arg);

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

int thread_pool_submit(thread_pool_t* pool,
                       void* (*func)(void*), void* arg)
{
    if (!pool || !func) return -1;

    task_node_t* task = (task_node_t*)malloc(sizeof(task_node_t));
    if (!task) return -1;

    task->func = func;
    task->arg = arg;

    pthread_mutex_lock(&pool->mutex);
    enqueue(pool, task);
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

void thread_pool_wait_all(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->mutex);
    while (pool->queue_size > 0 || pool->active_count > 0) {
        pthread_cond_wait(&pool->complete, &pool->mutex);
    }
    pthread_mutex_unlock(&pool->mutex);
}

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

int thread_pool_execute(void* (*func)(void*), void* args[],
                        int num_tasks, int num_threads)
{
    if (!func || !args || num_tasks <= 0) return -1;

    thread_pool_t* pool = thread_pool_create(num_threads);
    if (!pool) return -1;

    for (int i = 0; i < num_tasks; i++) {
        if (thread_pool_submit(pool, func, args[i]) != 0) {
            thread_pool_destroy(pool);
            return -1;
        }
    }

    thread_pool_wait_all(pool);
    thread_pool_destroy(pool);
    return 0;
}

int thread_pool_queue_size(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->mutex);
    int n = pool->queue_size;
    pthread_mutex_unlock(&pool->mutex);
    return n;
}

int thread_pool_active_count(thread_pool_t* pool) {
    pthread_mutex_lock(&pool->mutex);
    int n = pool->active_count;
    pthread_mutex_unlock(&pool->mutex);
    return n;
}
