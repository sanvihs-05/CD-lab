#include <stdio.h>

static volatile float sink;

int main(void) {
  volatile float x = 1.00001f;

  const float naive =
      (x * x * x * x * x) - (5.0f * x * x * x * x) + (10.0f * x * x * x) -
      (10.0f * x * x) + (5.0f * x) - 1.0f;

  const float horner =
      ((((x - 5.0f) * x + 10.0f) * x - 10.0f) * x + 5.0f) * x - 1.0f;

  sink = naive - horner;
  printf("%.9g\n", sink);
  return 0;
}
