#include <stdio.h>

static volatile float sink;

int main(void) {
  volatile float eps = 0.0001f;
  const float a = 1.0f + eps;
  const float b = 1.0f;
  const float c = 1.0f;
  const float d = 1.0f - eps;

  const float det = (a * d) - (b * c);
  sink = det;
  printf("%.9g\n", sink);
  return 0;
}
