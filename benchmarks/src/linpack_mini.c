#include <stdio.h>
#define N 60
#define REPEAT 8

static float a[N][N];
static float b[N][N];
static float c[N][N];

int main(void) {
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      a[i][j] = (float)((i + 1) * (j + 3)) * 0.001f;
      b[i][j] = (float)((i - j) * (i + j + 1)) * 0.0005f;
      c[i][j] = 0.0f;
    }
  }

  for (int rep = 0; rep < REPEAT; ++rep) {
    for (int i = 0; i < N; ++i) {
      for (int j = 0; j < N; ++j) {
        float sum = c[i][j];
        for (int k = 0; k < N; ++k) {
          sum += a[i][k] * b[k][j];
        }
        c[i][j] = sum;
      }
    }
  }

  float checksum = 0.0f;
  for (int i = 0; i < N; ++i) {
    checksum += c[i][i];
  }

  printf("%.9g\n", checksum);
  return 0;
}

