# NSSan Demo Script

This is the evaluator-facing demo. It shows NSSan detecting a real numerical bug and correctly passing clean code.

## One-Time Prep

On the Windows host:

```powershell
# Clone LLVM source (one-time, ~200 MB)
git clone --depth=1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git C:\llvm-project

# Build Docker image
docker build -f Dockerfile.test -t nssan-test .
```

The first native run builds patched Clang and can take ~20 minutes. Later runs reuse the cached Docker volumes and complete in seconds.

## Running the Demo

### Option A: Use the top-level script

```bash
./run.sh demo
```

### Option B: Direct Docker command

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./scripts/demo-native.sh
```

### Option C: Manual commands inside the container

```bash
# Enter container
docker run --rm -it \
  -v "${PWD}:/workspace" \
  -v "C:\llvm-project:/llvm-project" \
  -v nssan-llvm-src-cache:/llvm-src \
  -v nssan-llvm-build-cache:/llvm-build \
  -w /workspace \
  nssan-test bash

# Inside container:
bash ./scripts/install-native-nssan.sh

# Step 1: Normal compilation
/llvm-build/bin/clang ./demo/buggy.c -lm -o ./demo/out/buggy
./demo/out/buggy

# Step 2: With NSSan
/llvm-build/bin/clang -fsanitize=numerical ./demo/buggy.c -lm -o ./demo/out/buggy_san
./demo/out/buggy_san

# Step 3: Clean code with NSSan
/llvm-build/bin/clang -fsanitize=numerical ./demo/clean.c -o ./demo/out/clean_san
./demo/out/clean_san
```

## What You Should See

### Step 1: Normal Compilation (Silent Bug)

```
$ /llvm-build/bin/clang ./demo/buggy.c -lm -o ./demo/out/buggy
$ ./demo/out/buggy
Result: 4.62532043e-08
```

The program computes `1.0f - cosf(0.0001f)`. The true answer is ≈ 5.0e-8, but the `float` result is 4.625e-8 — **~7.5% error** due to catastrophic cancellation. The program reports nothing; the bug is silent.

### Step 2: With NSSan — FAILURE CASE (Bug Detected ✅)

```
$ /llvm-build/bin/clang -fsanitize=numerical ./demo/buggy.c -lm -o ./demo/out/buggy_san
$ ./demo/out/buggy_san
=== NUMERICAL SANITIZER: ERROR ===
  Type:     Catastrophic Cancellation
  Location: ./demo/buggy.c:6:20
  Operation: fsub
  float:    4.62532043e-08
  shadow:   4.9999999583255429e-08
  error:    7.4899279e-02x threshold exceeded
Result: 4.62532043e-08
```

NSSan catches the cancellation and reports:
- **Type**: Catastrophic Cancellation (subtraction of nearly equal values)
- **Location**: exact file, line, and column
- **Error**: the `float` value diverges from the `double` shadow by ~7.5%

### Step 3: Clean Code — WORKING CASE (No False Positive ✅)

```
$ /llvm-build/bin/clang -fsanitize=numerical ./demo/clean.c -o ./demo/out/clean_san
$ ./demo/out/clean_san
Result: 0.75
[NSSan] No numerical issues detected.
```

`clean.c` computes `0.25f + 0.5f`, which is exact in `float`. NSSan produces **no warnings** — confirming zero false positives on stable code.

## Full Test Suite

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./run_tests.sh
```

**Expected**: 12/12 passed, 2 true negatives, 0 false positives.

## Benchmark Run

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./benchmark.sh
```

**Expected**: Geometric mean overhead ~2.5× (vs Herbgrind's ~100×).
