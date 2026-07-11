#include "thread_manager.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int id;
    double value;
} task_param_t;

void* my_task(void* arg) {
    task_param_t* p = (task_param_t*)arg;
    double x = 0.0;
    for (int j = 0; j < 500000; j++)
        x += j * 0.001;
    printf("[%04d] result=%.2f\n", p->id, x);
    free(p);
    return NULL;
}

int main() {
    int n = thread_pool_optimal_count();
    printf("optimal threads: %d\n", n);

    task_param_t* args[200];
    for (int i = 0; i < 200; i++) {
        task_param_t* p = malloc(sizeof(task_param_t));
        p->id = i;
        p->value = i * 1.5;
        args[i] = p;
    }

    printf("executing 200 tasks...\n");
    thread_pool_execute(my_task, (void**)args, 200, 0);
    printf("all done!\n");

    return 0;
}
