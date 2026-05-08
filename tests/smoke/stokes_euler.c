#include <stdio.h>

static volatile float sink;

int main(void) {
  volatile float h = 0.0001f;
  float y = 100000000.0f;

  for (int i = 0; i < 100000; ++i) {
    y = y + h * 1.0f;
  }

  sink = y - 100000000.0f;
  printf("%.9g\n", sink);
  return 0;
}
