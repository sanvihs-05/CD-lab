#include <math.h>
#include <stdio.h>

static volatile float sink;

int main(void) {
  const float eps = 1.0e-8f;
  const float value = sqrtf(-eps);
  sink = value + 1.0f;
  printf("%.9g\n", sink);
  return 0;
}

