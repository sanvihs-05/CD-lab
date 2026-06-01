#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define PATHS 200000
#define STEPS 24

static uint32_t state = 2463534242u;

static float next_uniform(void) {
  state = 1664525u * state + 1013904223u;
  return (float)(state & 0x00ffffffu) / 16777216.0f;
}

int main(void) {
  const float s0 = 100.0f;
  const float strike = 105.0f;
  const float drift = 0.0002f;
  const float vol = 0.01f;
  float payoff_sum = 0.0f;

  for (int path = 0; path < PATHS; ++path) {
    float spot = s0;
    for (int step = 0; step < STEPS; ++step) {
      const float u = next_uniform() - 0.5f;
      spot = spot + drift * spot + vol * spot * u;
    }
    const float payoff = fmaxf(spot - strike, 0.0f);
    payoff_sum += payoff;
  }

  printf("%.9g\n", payoff_sum / PATHS);
  return 0;
}

