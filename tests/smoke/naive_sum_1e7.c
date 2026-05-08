#include <stdio.h>

static volatile float sink;

int main(void) {
  float sum = 0.0f;

  for (int i = 0; i < 1000000; ++i) {
    sum += 0.000001f;
  }

  sink = sum;
  printf("%.9g\n", sink);
  return 0;
}

