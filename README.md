# Numerical Stability Sanitizer (NSSan)

NSSan is an LLVM-based compiler sanitizer that detects floating-point precision loss at runtime. It shadows `float` operations in `double` precision, compares results, and emits source-level diagnostics when divergence exceeds a configurable threshold.

**Think of it as AddressSanitizer, but for numerical bugs.**

## Quick Start

```powershell
# 1. One-time setup: clone LLVM 18.1.8 source
git clone --depth=1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git C:\llvm-project

# 2. Build the Docker image + patched Clang (first run ~20 min, cached after)
.\build.ps1

# 3. Run tests, benchmarks, and demo
.\run.ps1
```

That's it. `.\run.ps1` executes the full 12-case test suite, 3-program benchmark harness, and the evaluator demo.

> **Note**: On Windows use `.\build.ps1` / `.\run.ps1`. On Linux/macOS use `./build.sh` / `./run.sh`.

### Run Individual Components

```powershell
.\run.ps1 tests    # 12-case smoke suite only
.\run.ps1 bench    # Benchmark overhead measurement only
.\run.ps1 demo     # Evaluator-facing demo only
```

## Setup From Scratch (detailed)

Start here if nothing is installed yet. Steps 1–4 are one-time; after that every
run finishes in seconds because the compiled Clang is cached in a Docker volume.

```powershell
# 1. Install prerequisites (one-time)
#    - Docker Desktop  -> https://www.docker.com/products/docker-desktop/  (must be RUNNING)
#    - Git             -> https://git-scm.com/download/win
#    - Node.js (only if you want the live playground) -> https://nodejs.org

# 2. Clone this repository
git clone https://github.com/sanvihs-05/CD-lab.git
cd CD-lab

# 3. Clone the LLVM 18.1.8 source next to it (one-time, ~200 MB)
git clone --depth=1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git C:\llvm-project

# 4. Build the Docker image + patched Clang (first build ~20 min, cached afterwards)
.\build.ps1

# 5. Run everything: 12-case tests + benchmarks + evaluator demo
.\run.ps1
```

**Verify it worked** — `.\run.ps1` should end with `Results: 12/12 passed | 2 true
negatives | 0 false positives`, a benchmark table (~6.4× geometric mean), and the
three-step demo (silent bug → caught → clean).

### Interactive UIs (optional)

```powershell
# A. Static overview dashboard — no Docker needed, opens in any browser
start demo\nssan-dashboard.html

# B. Live playground — type C, run it through the real NSSan toolchain
#    (needs Docker running + Node.js). Ctrl+C to stop. Opens http://localhost:7890
.\demo\live.ps1
```

> **Troubleshooting**
> - `docker ... cannot find the file specified` → Docker Desktop isn't running; start it and wait ~30s.
> - `.ps1 cannot be loaded` (execution policy) → run `powershell -ExecutionPolicy Bypass -File .\run.ps1`.
> - LLVM source not found → confirm it is at `C:\llvm-project` (or `%USERPROFILE%\llvm-project`).
> - On Linux/macOS use `./build.sh` / `./run.sh` instead of the `.ps1` scripts.

## Documentation

| Document | Contents |
|----------|----------|
| [DESIGN.md](DESIGN.md) | Problem statement, shadow execution approach, design decisions, alternatives considered |
| [IMPLEMENTATION.md](IMPLEMENTATION.md) | LLVM pass architecture, instrumented instructions, runtime library, Clang driver patches |
| [EVALUATION.md](EVALUATION.md) | Test results (12/12 pass), benchmark overhead (~6.1×), tool comparison, failure case analysis |

## What NSSan Detects

```c
// buggy.c — catastrophic cancellation
float result = 1.0f - cosf(0.0001f);  // cos ≈ 0.999999995 → subtraction kills precision
```

**Without NSSan**: program silently returns a wrong result.

**With NSSan** (`clang -g -fsanitize=numerical buggy.c`):
```
=== NUMERICAL SANITIZER: ERROR ===
  Type:     Catastrophic Cancellation
  Location: ./demo/buggy.c:6:23
  Operation: fsub
  float:    0
  shadow:   4.9999997475680402e-09
  error:    1.00000000000000000e+00x threshold exceeded
  precision: ~7.2 of 7.2 significant digits lost
=== NSSan SUMMARY ===
  Float operations checked: 2
  Numerical issues found:   1 unique site(s)
    Catastrophic Cancellation: 1
  Result: ISSUES DETECTED
```

Every run ends with an `=== NSSan SUMMARY ===` verdict printed by the runtime
itself — `ISSUES DETECTED` with a per-category breakdown, or `CLEAN` when no
divergence is found.

## How It Works

For each covered `float` operation, the LLVM pass:

1. Builds or reuses a `double`-precision shadow for each operand
2. Replays the operation in `double`
3. Calls the runtime to compare `float` result vs `double` shadow
4. Reports if relative error exceeds the threshold (default `1e-5`)

**Covered operations**: `fadd`, `fsub`, `fmul`, `fdiv`, `load`, `store`, `sqrtf`, `sinf`, `cosf`, `expf`, `logf`, `fmaf`

## Demo — How to Run and What to Expect

### Option A: Automated Demo

```bash
./run.sh demo
```

### Option B: Manual Docker Demo

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./scripts/demo-native.sh
```

### What You Will See

**Step 1 — Compile and run `buggy.c` normally:**
```
1. Compile normally
$ /llvm-build/bin/clang -g ./demo/buggy.c -lm -o ./demo/out/buggy
$ ./demo/out/buggy
Result: 0
```
The program runs silently with a **wrong result** — `1.0f - cosf(0.0001f)` collapses
to exactly `0` in `float` (true answer ≈ 5.0e-9), a total loss of precision.

**Step 2 — Compile with NSSan (FAILURE CASE — expected):**
```
2. Compile with NSSan
$ /llvm-build/bin/clang -g -fsanitize=numerical ./demo/buggy.c -lm -o ./demo/out/buggy_san
$ ./demo/out/buggy_san
=== NUMERICAL SANITIZER: ERROR ===
  Type:     Catastrophic Cancellation
  Location: ./demo/buggy.c:6:23
  Operation: fsub
  float:    0
  shadow:   4.9999997475680402e-09
  error:    1.00000000000000000e+00x threshold exceeded
  precision: ~7.2 of 7.2 significant digits lost
Result: 0
=== NSSan SUMMARY ===
  Float operations checked: 2
  Numerical issues found:   1 unique site(s)
    Catastrophic Cancellation: 1
  Result: ISSUES DETECTED
```
NSSan **catches the bug** and reports the location, operation, and error magnitude —
the `double` shadow retains the correct ~5e-9 answer while the `float` collapsed to 0.

**Step 3 — Compile `clean.c` with NSSan (WORKING CASE):**
```
3. Clean baseline
$ /llvm-build/bin/clang -g -fsanitize=numerical ./demo/clean.c -o ./demo/out/clean_san
$ ./demo/out/clean_san
Result: 0.75
```
`0.25f + 0.5f` is exact in `float` (both are powers of two), so NSSan stays silent —
correctly producing **no false positives** on numerically stable code.

## Repository Layout

```
.
├── build.sh                    # One-command build
├── run.sh                      # One-command run (tests + bench + demo)
├── DESIGN.md                   # Design document
├── IMPLEMENTATION.md           # LLVM implementation details
├── EVALUATION.md               # Evaluation metrics and results
├── README.md                   # This file
│
├── lib/NSSanPass.cpp           # LLVM instrumentation pass (~460 lines)
├── runtime/NSSanRuntime.cpp    # Runtime check + report library (~180 lines)
├── include/nssan/NSSanRuntime.h# Runtime API header
├── CMakeLists.txt              # Build system
│
├── tests/smoke/                # 12 test cases (src)
├── run_tests.sh                # Test runner with pass/fail reporting
│
├── benchmarks/src/             # 3 benchmark programs
├── benchmark.sh                # Overhead measurement harness
│
├── demo/buggy.c                # Demo: numerically unstable program
├── demo/clean.c                # Demo: numerically stable program
├── demo/DEMO_SCRIPT.md         # Step-by-step demo guide
│
├── scripts/
│   ├── build-native-clang.sh   # Builds patched Clang with -fsanitize=numerical
│   ├── install-native-nssan.sh # Installs pass + runtime into Clang resource dir
│   └── demo-native.sh          # Scripted demo sequence
│
├── tools/nssan-clang.ps1       # Legacy out-of-tree wrapper (Windows)
└── Dockerfile.test             # Docker build environment (Ubuntu 24.04)
```

## Native Docker Setup

### Prerequisites

- **Docker Desktop** installed and running
- **LLVM 18.1.8 source** cloned on host (one-time, ~200 MB):

```powershell
git clone --depth=1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git C:\llvm-project
```

### How It Works

The native workflow uses Docker volumes to cache the build:

- `nssan-llvm-src-cache` — patched LLVM worktree
- `nssan-llvm-build-cache` — compiled Clang binaries

The first build compiles Clang from source (~20 min). Subsequent runs detect the cached build and skip recompilation.

### Clang Driver Patches

The patched Clang accepts `-fsanitize=numerical` natively. Five Clang source files are modified:

| File | Change |
|------|--------|
| `Sanitizers.def` | Register `Numerical` sanitizer kind |
| `SanitizerArgs.h` | Add `NeedsNumericalRt` flag |
| `SanitizerArgs.cpp` | Enable numerical sanitizer group |
| `CommonArgs.cpp` | Add pass plugin + debug flags + link runtime |
| `Linux.cpp` | Add resource dir to library search path |

## Test Suite

Run with `./run.sh tests` or directly:

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./run_tests.sh
```

**Results: 12/12 passed | 2 true negatives | 0 false positives**

## Benchmarks

Run with `./run.sh bench` or directly:

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./benchmark.sh
```

**Geometric mean overhead: ~6.1× (vs Herbgrind's ~100×) — roughly 16× faster than Herbgrind**

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `NSSAN_THRESHOLD` | `1e-5` | Relative error threshold for reporting |
| `NSSAN_HALT_ON_ERROR` | `0` | Set to `1` to abort on first error |

## Current Scope

- Strongest on scalar `float` code compiled with debug info (`-g`)
- Uses `double` shadow precision (sufficient for detecting `float` instabilities)
- Does not yet cover: vectorized (SIMD) code, heap-aware shadow propagation, `double→long double` shadowing
- Reports are deduplicated by `(file, line, column, opcode)` to avoid flooding loops
