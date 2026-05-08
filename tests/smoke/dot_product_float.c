#include <stdio.h>

static volatile float sink;

int main(void) {
  volatile float big = 100000000.0f;
  volatile float unit = 1.0f;
  float acc = 0.0f;

  for (int i = 0; i < 200000; ++i) {
    acc += big * unit;
    acc += unit * unit;
    acc += (-big) * unit;
  }

  sink = acc;
  printf("%.9g\n", sink);
  return 0;
}
