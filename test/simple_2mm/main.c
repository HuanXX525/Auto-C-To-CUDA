int D[800][1200];
int tmp[800][900];
int C[900][1200];

int main() {
  int i, j, k;
  int beta;

  for (i = 0; i < 800; i++)
    for (j = 0; j < 1200; j++)
    {
      D[i][j] *= beta;
      for (k = 0; k < 900; ++k)
        D[i][j] += tmp[i][k] * C[k][j];
    }

  return 0;
}
