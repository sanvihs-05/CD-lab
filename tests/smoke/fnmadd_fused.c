#include <math.h>
#include <stdio.h>

static volatile float sink;

int main(void) {
  volatile float a = -765488.9375f;
  volatile float b = 712668.625f;
  volatile float c = -554176086016.0f;

  sink = fmaf(a, b, c);
  printf("%.9g\n", sink);
  return 0;
}
