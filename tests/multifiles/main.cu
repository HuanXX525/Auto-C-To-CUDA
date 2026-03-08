#include <stdio.h>
#include"head.h"
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1

int main()
{
  int i_nom_1;
  int i;
  for (i = 1; i <= 10; i += 1) {
    loop();
  }
  for (i_nom_1 = 1; i_nom_1 <= 10; i_nom_1 += 1) {
    printf("%d ",arr[1 * i_nom_1 + -1]);
  }
  return 0;
}
