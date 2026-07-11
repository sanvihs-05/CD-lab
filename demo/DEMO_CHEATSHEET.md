# NSSan — Live Demo Cheat Sheet

A one-page guide for demonstrating NSSan to an evaluator. Left column = what you
type/show. Right column = what you say.

## 0. One-time prep (do this BEFORE the demo — takes ~20 min the first time)

```powershell
# LLVM source is already cloned at C:\llvm-project
.\build.ps1        # builds Docker image + patched Clang (cached afterwards)
.\run.ps1          # warm-up: runs tests + bench + demo once so caches are hot
```

After this, every later run finishes in seconds.

---

## 1. The 30-second pitch (say first)

> "There are compiler sanitizers for memory bugs — AddressSanitizer — and for
> data races — ThreadSanitizer. But there is **no mainstream sanitizer for
> numerical bugs**: cases where `float` arithmetic silently loses precision and
> the program returns a wrong-but-plausible answer. NSSan fills that gap. It's
> an LLVM pass that re-runs every `float` operation in `double` precision in
> parallel — a *shadow* execution — and flags any place where the two diverge.
> You turn it on exactly like ASan: `clang -fsanitize=numerical`."

---

## 2. Show the bug is real and silent

```powershell
.\run.ps1 demo
```

**Step 1 — compile normally.** Point at the output:

> "This program computes `1.0f - cosf(0.0001f)`. The true answer is about
> 5.0e-9, but in float the cosine rounds to exactly 1.0, so the result collapses
> to **exactly 0 — 100% wrong**. Notice the program prints a result and exits
> cleanly. The bug is completely silent. This is catastrophic cancellation:
> subtracting two nearly-equal numbers."

**Step 2 — compile with NSSan.** Point at the ERROR block:

> "Same program, now with `-fsanitize=numerical`. NSSan catches it and tells me
> exactly *what* (catastrophic cancellation), *where* (file, line, column),
> the *float vs double* values — float is 0 but the double shadow kept the
> correct ~5e-9 — and, the part I added, that **all ~7 of float's significant
> digits were destroyed**. At the end it prints a summary verdict: ISSUES
> DETECTED, one site, one cancellation."

**Step 3 — clean code, no false positive.** Point at the CLEAN summary:

> "And on numerically-stable code — `0.25f + 0.5f`, which is exact in float —
> NSSan stays quiet and reports CLEAN. No false positives. That's important:
> a sanitizer nobody trusts gets turned off."

---

## 3. Show it's not a toy — the test suite

```powershell
.\run.ps1 tests
```

> "12-case suite covering distinct instability classes — cancellation,
> accumulation drift, NaN, infinity, ill-conditioned matrices, a stiff ODE.
> **12/12 pass, including 2 true negatives (Kahan summation, exact baseline)
> with zero false positives.**"

## 4. Show it's practical — benchmarks

```powershell
.\run.ps1 bench
```

> "Overhead is about **6× geometric mean** on real kernels — LINPACK, Monte
> Carlo option pricing, Navier-Stokes. The closest comparable tool, Herbgrind,
> uses arbitrary-precision MPFR and runs ~100×, so we're roughly **15× faster**
> and usable in a normal edit-compile-test loop."

---

## 5. If asked "how does it actually work?" (whiteboard / point at code)

- **`lib/NSSanPass.cpp`** — LLVM IR pass. For each `float` op it builds a
  `double` shadow of each operand (a `DenseMap<Value*, Value*>`), replays the op
  in `double`, then inserts a call to the runtime check. Covers `fadd/fsub/
  fmul/fdiv`, `load`/`store`, and math calls/intrinsics (`sqrtf`, `cosf`,
  `expf`, `fmaf`, …).
- **`runtime/NSSanRuntime.cpp`** — compares float result vs double shadow.
  Reports if relative error > threshold (default `1e-5`); classifies the issue;
  dedupes by `(file,line,col,opcode)` so loops don't flood; prints the
  end-of-run summary via `std::atexit`.
- **Clang driver patches (5 files)** — make `-fsanitize=numerical` a first-class
  flag that auto-loads the pass plugin and links the runtime, matching ASan UX.

## 6. Likely questions & crisp answers

- **"Why `double` and not arbitrary precision?"** Double gives ~16 digits vs
  float's ~7 — plenty of headroom to *detect* float instability — at ~6×
  instead of MPFR's ~100×. It's a deliberate accuracy/speed trade-off.
- **"What can it miss?"** Bugs that are already unstable in `double` (the shadow
  would be wrong too), SIMD/vectorized code, and shadow propagation through
  `memcpy`/unions. These are documented limitations, not surprises.
- **"How is `digits lost` computed?"** A relative error `e` leaves about
  `-log10(e)` correct significant digits; float carries ~7.2, so lost ≈
  `7.2 - (-log10(e))`.
- **"Is the CLEAN message real or faked?"** Real — the runtime itself counts
  checks and issues and prints the verdict at exit. (We removed an earlier
  shell-script stand-in.)
