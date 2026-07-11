#include <stdio.h>

int main(void) {
  /* volatile keeps the add from being constant-folded away, so NSSan actually
     instruments and checks the fadd -- and reports CLEAN on stable code. */
  volatile float a = 0.25f;
  volatile float b = 0.5f;
  float result = a + b; /* 0.25 and 0.5 are exact powers of two: no rounding */

  printf("Result: %.9g\n", result);
  return 0;
}
