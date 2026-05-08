#include <stdio.h>

static volatile float sink;

int main(void) {
  float sum = 0.0f;

  for (int i = 0; i < 16; ++i) {
    sum += 0.25f;
  }

  sink = sum * 2.0f;
  printf("%.9g\n", sink);
  return 0;
}

