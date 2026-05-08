# Benchmarks

The synopsis calls for three representative benchmark families:

- dense linear algebra
- Navier-Stokes style simulation
- Monte Carlo pricing

This repository does not bundle those third-party workloads yet, but the intended measurement loop is:

1. Build the benchmark normally.
2. Rebuild it with `tools/nssan-clang.ps1`.
3. Measure wall-clock time and peak memory over five runs.
4. Report geometric-mean overhead relative to baseline.

Recommended first additions:

- single-precision LINPACK
- a compact CFD mini-app
- a Monte Carlo Black-Scholes pricer

