#include "thread_manager.h"
#include <stdio.h>
#include <stdlib.h>
int main_task(int argc, char **argv);
int main()
{
    thread_pool_run(main_task, "args.json", "./thread_log.log");
    return 0;
}