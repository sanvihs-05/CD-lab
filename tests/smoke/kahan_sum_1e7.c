#include <stdio.h>

static volatile float sink;

int main(void) {
  float sum = 0.0f;
  float c = 0.0f;

  for (int i = 0; i < 16384; ++i) {
    const float term = 0.0009765625f;
    const float y = term - c;
    const float t = sum + y;
    c = (t - sum) - y;
    sum = t;
  }

  sink = sum;
  printf("%.9g\n", sink);
  return 0;
}
