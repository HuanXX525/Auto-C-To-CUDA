#include "nbody.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1

int main(int argc,char *argv[])
{
  int i_nom_1;
  int i;
// 设置随机种子
  srand((time(((void *)0))));
// 参数设置
  int num_particles = 100;
// 粒子数量
  double G = 1.0;
// 万有引力常数
  double dt = 0.01;
// 时间步长
  int steps = 1000;
// 模拟步数
  int print_interval = 100;
// 输出间隔
// 从命令行读取参数（可选）
  if (argc >= 2) {
    num_particles = atoi(argv[1]);
    if (num_particles <= 0) {
      fprintf(stderr,"\351\224\231\350\257\257: \347\262\222\345\255\220\346\225\260\351\207\217\345\277\205\351\241\273\345\244\247\344\272\2160\n");
      return 1;
    }
  }
  if (argc >= 3) {
    steps = atoi(argv[2]);
    if (steps <= 0) {
      fprintf(stderr,"\351\224\231\350\257\257: \346\250\241\346\213\237\346\255\245\346\225\260\345\277\205\351\241\273\345\244\247\344\272\2160\n");
      return 1;
    }
  }
  printf("N-Body \347\262\222\345\255\220\346\250\241\346\213\237\n");
  printf("================\n");
  printf("\347\262\222\345\255\220\346\225\260\351\207\217: %d\n",num_particles);
  printf("\346\250\241\346\213\237\346\255\245\346\225\260: %d\n",steps);
  printf("\346\227\266\351\227\264\346\255\245\351\225\277: %.6f\n",dt);
  printf("\344\270\207\346\234\211\345\274\225\345\212\233\345\270\270\346\225\260: %.6f\n\n",G);
// 初始化系统
  NBodySystem system;
  init_system(&system,num_particles,G,dt);
  init_particles(&system);
  printf("\345\210\235\345\247\213\347\212\266\346\200\201:\n");
  print_particles(&system);
  printf("\n\345\274\200\345\247\213\346\250\241\346\213\237...\n\n");
// 开始计时
  clock_t start = clock();
// 主模拟循环
  for (i = 1; i <= (steps + 0) / 1; i += 1) {
    step(&system);
// 定期输出进度
    if ((1 * i + -1) % print_interval == 0) {
      printf("\345\267\262\345\256\214\346\210\220 %d \346\255\245 (\345\205\261 %d \346\255\245)\n",1 * i + -1,steps);
    }
  }
// 结束计时
  clock_t end = clock();
  double elapsed_time = ((double )(end - start)) / ((__clock_t )1000000);
  printf("\n\346\250\241\346\213\237\345\256\214\346\210\220\357\274\201\n");
  printf("\346\200\273\350\200\227\346\227\266: %.6f \347\247\222\n",elapsed_time);
  printf("\345\271\263\345\235\207\346\257\217\346\255\245\350\200\227\346\227\266: %.6f \347\247\222\n",elapsed_time / steps);
// 输出最终状态（只输出前10个粒子）
  printf("\n\346\234\200\347\273\210\347\212\266\346\200\201 (\345\211\21510\344\270\252\347\262\222\345\255\220):\n");
  int print_count = num_particles < 10?num_particles : 10;
  printf("\347\262\222\345\255\220\346\225\260\351\207\217: %d\n",system . n);
  printf("\346\227\266\351\227\264\346\255\245\351\225\277: %.6f\n",system . dt);
  printf("\344\270\207\346\234\211\345\274\225\345\212\233\345\270\270\346\225\260: %.6f\n\n",system . G);
  for (i_nom_1 = 1; i_nom_1 <= (print_count + 0) / 1; i_nom_1 += 1) {
    printf("\347\262\222\345\255\220 %d:\n",1 * i_nom_1 + -1);
    printf("  \344\275\215\347\275\256: (%.6f, %.6f, %.6f)\n",system . particles[1 * i_nom_1 + -1] . x,system . particles[1 * i_nom_1 + -1] . y,system . particles[1 * i_nom_1 + -1] . z);
    printf("  \351\200\237\345\272\246: (%.6f, %.6f, %.6f)\n",system . particles[1 * i_nom_1 + -1] . vx,system . particles[1 * i_nom_1 + -1] . vy,system . particles[1 * i_nom_1 + -1] . vz);
    printf("  \350\264\250\351\207\217: %.6f\n",system . particles[1 * i_nom_1 + -1] . mass);
  }
  if (num_particles > 10) {
    printf("... (\347\234\201\347\225\245\345\205\266\344\273\226 %d \344\270\252\347\262\222\345\255\220)\n",num_particles - 10);
  }
// 释放内存
  free_system(&system);
  return 0;
}
