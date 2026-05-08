#include <math.h>
#include <stdio.h>

static volatile float sink;

int main(void) {
  const float x = 0.001f;
  sink = 1.0f - cosf(x);
  printf("%.9g\n", sink);
  return 0;
}

