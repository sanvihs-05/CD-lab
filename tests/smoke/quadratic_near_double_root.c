#include <math.h>
#include <stdio.h>

static volatile float sink;

int main(void) {
  const float a = 1.0f;
  const float b = 100000000.0f;
  const float c = 1.0f;

  const float disc = sqrtf((b * b) - (4.0f * a * c));
  const float root = (-b + disc) / (2.0f * a);

  sink = root;
  printf("%.9g\n", sink);
  return 0;
}

