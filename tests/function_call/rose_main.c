#include <math.h>
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
int arr[10] = {(1), (2), (3), (4), (5), (6), (7), (8), (9), (0)};

int getN()
{
  return 4;
}

int main()
{
  int i_nom_1;
  int i;
  for (i = 1; i <= 10; i += 1) {
    arr[1 * i + -1] += pow((1 * i + -1),(getN()));
  }
  for (i_nom_1 = 1; i_nom_1 <= 10; i_nom_1 += 1) {
    printf("%d ",arr[1 * i_nom_1 + -1]);
  }
  return 0;
}
