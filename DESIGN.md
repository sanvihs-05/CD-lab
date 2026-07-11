# NSSan — Design Document

## Problem Statement

Floating-point arithmetic is inherently approximate. IEEE 754 `float` (32-bit) provides ~7 decimal digits of precision, which is often insufficient for numerically sensitive computations. Errors accumulate silently through:

- **Catastrophic cancellation**: subtraction of nearly equal values amplifies relative error
- **Absorption**: adding a tiny value to a large one loses the small operand entirely
- **Overflow / NaN propagation**: uncontrolled growth produces infinities or NaN without warning

These bugs are insidious because the program runs to completion with *plausible-looking* but *wrong* results. Unlike memory errors (caught by AddressSanitizer) or data races (caught by ThreadSanitizer), there is no mainstream compiler sanitizer for numerical stability.

**NSSan fills this gap.**

## Approach: Shadow Execution in Higher Precision

NSSan uses the **shadow execution** strategy: every covered `float` operation is replayed in `double` (64-bit) precision in parallel. At each instrumented operation, the runtime compares the `float` result against the `double` shadow result. If the relative error exceeds a configurable threshold, a diagnostic is emitted with source location and error magnitude.

```
                        ┌──────────────────────────┐
                        │   Source Program (.c)     │
                        └────────────┬─────────────┘
                                     │  clang -fsanitize=numerical
                                     ▼
                        ┌──────────────────────────┐
                        │    LLVM IR (float ops)    │
                        └────────────┬─────────────┘
                                     │  NSSanPass (instrumentation)
                                     ▼
                ┌────────────────────────────────────────────┐
                │            Instrumented IR                 │
                │                                            │
                │  For each float op:                        │
                │    1. Build/reuse double shadow operands   │
                │    2. Replay operation in double           │
                │    3. Call __nssan_check_float_op(...)     │
                └────────────────────┬───────────────────────┘
                                     │  Link with NSSanRuntime
                                     ▼
                ┌────────────────────────────────────────────┐
                │          Instrumented Binary               │
                │                                            │
                │  At runtime:                               │
                │    • Shadow map tracks ptr→double values   │
                │    • Check function compares float/double  │
                │    • Reports divergence above threshold    │
                └────────────────────────────────────────────┘
```

### Why This Works

The key insight is that `double` has ~16 decimal digits of precision versus `float`'s ~7. When a `float` computation loses precision (e.g., catastrophic cancellation), the `double` shadow retains the "more correct" answer. The divergence between the two reveals the precision loss.

## Key Design Decisions

### 1. Shadow Precision: `double` vs `long double` vs Arbitrary Precision

| Option | Precision | Overhead | Portability |
|--------|-----------|----------|-------------|
| `double` (chosen) | ~16 digits | Low (2–5×) | Universal |
| `long double` | ~18–34 digits | Medium (5–10×) | Platform-dependent (80-bit on x86, 128-bit on some) |
| MPFR / arbitrary precision | Unlimited | Very high (~100×) | Requires external library |

**Decision**: We use `double` because it provides sufficient headroom for detecting `float` instabilities while keeping overhead practical. The Herbgrind tool (which uses MPFR) achieves higher fidelity but at ~100× slowdown, making it impractical for routine CI use.

### 2. Instrumentation Level: LLVM IR vs Source-to-Source vs Binary

| Approach | Pros | Cons |
|----------|------|------|
| **LLVM IR pass** (chosen) | Language-agnostic, sees optimized code, integrates with Clang | Requires LLVM build infrastructure |
| Source-to-source | Easier to prototype | Fragile, misses compiler-introduced operations |
| Binary instrumentation (Valgrind/DynamoRIO) | No recompilation needed | Very high overhead, no source info |

**Decision**: LLVM IR instrumentation gives us the best balance of accuracy, performance, and diagnostics. We see the actual operations the compiler generates (including intrinsics) and can attach debug info for source-level reports.

### 3. Per-Location Deduplication

Numerical issues in loops would flood stderr with millions of identical reports. NSSan reports **once per unique (file, line, column, opcode) tuple**, keeping output actionable.

### 4. Configurable Threshold

The default relative error threshold is `1e-5`. Users can tune it via `NSSAN_THRESHOLD` for their domain. A tighter threshold (e.g., `1e-8`) catches subtler issues; a looser one reduces noise.

### 5. Clang Driver Integration

Rather than requiring manual `-fplugin=... -Xlinker ...` flags, NSSan patches the Clang driver to accept `-fsanitize=numerical`, which automatically loads the pass plugin and links the runtime library — matching the UX of ASan/TSan/UBSan.

## Alternatives Considered

### Herbgrind (Valgrind-based)

- Uses MPFR for arbitrary-precision shadow execution
- Extremely accurate but ~100× runtime overhead
- Requires Valgrind infrastructure (not available on all platforms)
- **Our advantage**: NSSan achieves ~6.4× overhead (measured, debug builds) with sufficient detection for common instabilities — roughly 15× faster than Herbgrind

### FPChecker (LLVM-based)

- Checks for NaN/Inf propagation only
- Does not detect subtle precision loss (cancellation, drift)
- **Our advantage**: NSSan detects relative error divergence, covering a broader class of numerical bugs

### Interval Arithmetic

- Tracks error bounds instead of single shadow values
- Overly conservative: produces many false positives
- Higher implementation complexity
- **Our advantage**: Single shadow value comparison is simpler and produces fewer false alarms

### Static Analysis

- Tools like Fluctuat or Gappa analyze error bounds at compile time
- Cannot handle data-dependent control flow
- Limited to small code regions
- **Our advantage**: Dynamic analysis covers all executed paths with actual inputs

## Scope and Limitations

### What NSSan Covers

- Scalar `float` arithmetic: `fadd`, `fsub`, `fmul`, `fdiv`
- Memory-backed float values via `load`/`store` shadow tracking
- Math library calls and intrinsics: `sqrtf`, `sinf`, `cosf`, `expf`, `logf`, `fmaf`/`fmuladd`

### Known Limitations

- **No `double`→`long double` shadowing**: NSSan only shadows `float`→`double`. Programs with `double` precision issues are out of scope.
- **No vectorized code**: SIMD/SSE operations are not instrumented.
- **No heap-aware shadow propagation**: Shadows are tracked by stack address; pointer aliasing through `memcpy` or unions is not followed.
- **No interprocedural shadow passing**: Function arguments get a fresh shadow via `fpext`; returning shadow values across call boundaries is not implemented.
