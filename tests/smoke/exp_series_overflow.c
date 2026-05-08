#include <stdio.h>

static volatile float sink;

int main(void) {
  float value = 1.0f;

  for (int i = 0; i < 16; ++i) {
    value = value * 100000.0f;
  }

  sink = value + 1.0f;
  printf("%.9g\n", sink);
  return 0;
}

