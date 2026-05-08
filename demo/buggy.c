#include <math.h>
#include <stdio.h>

int main(void) {
  volatile float x = 0.0001f;
  float result = 1.0f - cosf(x);

  printf("Result: %.9g\n", result);
  return 0;
}
