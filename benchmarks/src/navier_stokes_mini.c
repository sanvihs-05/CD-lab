#include <stdio.h>

#define NX 50
#define NY 50
#define STEPS 150

static float u[NX][NY];
static float next_u[NX][NY];

int main(void) {
  for (int i = 0; i < NX; ++i) {
    for (int j = 0; j < NY; ++j) {
      u[i][j] = (float)((i * j) % 17) * 0.01f;
      next_u[i][j] = 0.0f;
    }
  }

  for (int step = 0; step < STEPS; ++step) {
    for (int i = 1; i < NX - 1; ++i) {
      for (int j = 1; j < NY - 1; ++j) {
        const float center = u[i][j];
        const float laplace = u[i - 1][j] + u[i + 1][j] + u[i][j - 1] +
                              u[i][j + 1] - 4.0f * center;
        const float advect = 0.02f * center * (u[i + 1][j] - u[i - 1][j]);
        next_u[i][j] = center + 0.08f * laplace - advect;
      }
    }

    for (int i = 1; i < NX - 1; ++i) {
      for (int j = 1; j < NY - 1; ++j) {
        u[i][j] = next_u[i][j];
      }
    }
  }

  float checksum = 0.0f;
  for (int i = 1; i < NX - 1; ++i) {
    checksum += u[i][NY / 2];
  }

  printf("%.9g\n", checksum);
  return 0;
}

