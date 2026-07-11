# NSSan — Evaluation

## Test Suite Overview

NSSan includes a 12-case smoke suite covering distinct classes of numerical instability. Each test is a self-contained C program compiled with `-fsanitize=numerical` and run with the default threshold (`1e-5` unless noted).

The full suite is executed by `./run_tests.sh` (or `./run.sh` which wraps it).

## Test Case Results

| # | Test Case | Category | Expected | Result | Description |
|---|-----------|----------|----------|--------|-------------|
| 01 | `quadratic_near_double_root` | Cancellation | Report ✅ | **PASS** | Quadratic formula with `b=1e8` causes cancellation in `(-b + sqrt(b²-4ac))` |
| 02 | `naive_sum_1e7` | Accumulation drift | Report ✅ | **PASS** | Summing `0.000001f` one million times — float drifts from true value |
| 03 | `kahan_sum_1e7` | Compensated sum | Clean ✅ | **PASS** | Kahan summation algorithm — error-compensated, no report expected (true negative) |
| 04 | `dot_product_float` | Cancellation | Report ✅ | **PASS** | Dot product with large positive/negative terms that cancel |
| 05 | `exp_series_overflow` | Infinity | Report ✅ | **PASS** | Repeated multiplication by 100000 overflows to `Inf` |
| 06 | `sqrt_negative_eps` | NaN | Report ✅ | **PASS** | `sqrtf(-1e-8)` produces NaN |
| 07 | `horner_vs_naive_poly` | Polynomial evaluation | Report ✅ | **PASS** | Naive vs Horner evaluation of `(x-1)^5` near `x=1.00001` — difference reveals instability |
| 08 | `matrix_ill_cond` | Ill-conditioned matrix | Report ✅ | **PASS** | 2×2 determinant `(1+ε)(1-ε) - 1·1` — cancellation in `det` |
| 09 | `stokes_euler` | Stiff ODE | Report ✅ | **PASS** | Forward Euler on `y' = 1` with `y₀=1e8` — small `h` absorbed by large `y` |
| 10 | `no_cancel_baseline` | Clean baseline | Clean ✅ | **PASS** | Sum of `0.25f` (exact in float) × 16, then `×2` — no instability (true negative) |
| 11 | `fnmadd_fused` | FMA rounding | Report ✅ | **PASS** | `fmaf(a,b,c)` with large operands — rounding differs between float and double FMA (threshold `1e-8`) |
| 12 | `catastrophic_1minus_cos` | Cancellation | Report ✅ | **PASS** | Classic `1 - cos(x)` for small `x` — cancellation near 1.0 |

### Summary

| Metric | Value |
|--------|-------|
| Total test cases | 12 |
| Passed | **12 / 12** |
| True positives | 10 |
| True negatives | 2 |
| False positives | 0 |
| False negatives | 0 |
| Detection rate | **100%** |

## Benchmark Overhead

Three compute-intensive programs are compiled with and without `-fsanitize=numerical` at `-O0 -g` (debug mode — the realistic use case for sanitizers, matching ASan/UBSan workflows). Each is timed 5 runs and averaged. Workloads are sized so the baseline is well above process-startup noise, keeping the ratio stable. The benchmark harness is in `benchmark.sh`.

| Program | Description | Baseline | Instrumented | Overhead |
|---------|-------------|----------|--------------|----------|
| **LINPACK** | 60×60 dense matrix multiply (500 reps) | ~0.33s | ~1.79s | ~5.5× |
| **Monte Carlo** | 1.5M-path option pricing (24 steps each) | ~0.32s | ~1.54s | ~4.8× |
| **Navier-Stokes** | 50×50 grid, 12000 time steps | ~0.25s | ~2.48s | ~9.9× |

| | |
|---|---|
| **Geometric mean overhead** | **~6.4×** |

> **Note**: Exact timings vary by hardware and Docker configuration. The overhead is per-operation shadow arithmetic plus a runtime check call, so float-dense kernels (Navier-Stokes) cost more than mixed ones. Herbgrind's ~100× overhead includes Valgrind's baseline JIT cost. Run `.\run.ps1 bench` to reproduce on your system.

## Comparison with Existing Tools

| Tool | Technique | Overhead | Detects Cancellation | Detects NaN/Inf | Source Location | Integration |
|------|-----------|----------|---------------------|-----------------|-----------------|-------------|
| **NSSan** (this project) | Shadow `float→double` via LLVM pass | **~6.4×** | ✅ Yes | ✅ Yes | ✅ File:line:col | `clang -fsanitize=numerical` |
| **Herbgrind** | Shadow via MPFR (Valgrind) | ~100× | ✅ Yes | ✅ Yes | ⚠️ Limited | Valgrind command |
| **FPChecker** | LLVM pass, checks NaN/Inf only | ~2× | ❌ No | ✅ Yes | ✅ Yes | Compiler plugin |
| **Verrou** | Monte Carlo rounding (Valgrind) | ~5–10× | ⚠️ Statistical | ❌ No | ⚠️ Limited | Valgrind command |

### Key Advantages of NSSan

1. **~15× faster than Herbgrind**: ~6.4× vs ~100× overhead, making it practical for development testing
2. **Broad detection**: Catches cancellation, drift, NaN, and Inf — not just special values
3. **First-class Clang integration**: `-fsanitize=numerical` matches ASan/UBSan UX
4. **Actionable diagnostics**: Reports source file, line, column, operation, actual vs shadow values

## Failure Case Analysis

### Known Limitation: Double-Precision Programs

NSSan only shadows `float → double`. A program that is already numerically unstable in `double` precision will not be caught, because the shadow (`double`) would exhibit the same instability as the original.

**Example scenario**: A `double` Kahan sum would not benefit from NSSan because both the original and shadow execute in `double`.

### False Positive Avoidance

The two-tier filtering (absolute tolerance + relative threshold) prevents false positives on:
- Values near zero where relative error is meaningless
- Exact float arithmetic (e.g., powers of 2)

Test cases `t03` (Kahan sum) and `t10` (exact baseline) verify that NSSan produces **zero false positives** on clean code.

## Reproducibility

All results can be reproduced by running:

```bash
# Build everything
./build.sh

# Run tests + benchmarks + demo
./run.sh
```

See the [README](README.md) for prerequisites and detailed instructions.
