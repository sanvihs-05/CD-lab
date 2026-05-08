# Numerical Stability Sanitizer

NSSan is an LLVM-based numerical-stability sanitizer for floating-point code. It shadows covered `float` operations in higher precision, compares the instrumented result against the shadow result at runtime, and emits source-level diagnostics when the divergence crosses a threshold.

This repo now supports two ways to run NSSan:

- Native patched Clang driver in Docker: `clang -fsanitize=numerical ...`
- Legacy out-of-tree wrapper: `tools/nssan-clang.ps1`

The native Docker path is the verified evaluator-facing flow.

## Verified Status

- Native `clang -fsanitize=numerical` works inside Docker with LLVM 18.1.8.
- The full native smoke suite in [run_tests.sh](/C:/Users/sanvi/OneDrive/Desktop/cd%20lab/run_tests.sh) passed `12/12`, with `2` true negatives and `0` false positives.
- The native driver patch currently spans five Clang files:
  - `clang/include/clang/Basic/Sanitizers.def`
  - `clang/include/clang/Driver/SanitizerArgs.h`
  - `clang/lib/Driver/SanitizerArgs.cpp`
  - `clang/lib/Driver/ToolChains/CommonArgs.cpp`
  - `clang/lib/Driver/ToolChains/Linux.cpp`

## What NSSan Does

For each covered `float` operation, the pass:

1. builds or reuses a higher-precision shadow value for each operand
2. replays the operation in higher precision
3. calls the runtime with file, line, column, opcode, actual result, and shadow result

The current MVP covers:

- `fadd`, `fsub`, `fmul`, `fdiv`
- stack-backed `float` values through `load` and `store`
- selected math calls and intrinsics such as `sqrtf`, `sinf`, `cosf`, `expf`, `logf`, and `fmaf`

## Repository Layout

```text
.
|-- CMakeLists.txt
|-- Dockerfile.test
|-- benchmark.sh
|-- demo/
|-- include/nssan/NSSanRuntime.h
|-- lib/NSSanPass.cpp
|-- runtime/NSSanRuntime.cpp
|-- run_tests.sh
|-- scripts/
|-- tests/
`-- tools/nssan-clang.ps1
```

## Native Docker Setup

Clone LLVM 18.1.8 on the Windows host once:

```powershell
git clone --depth=1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git C:\llvm-project
```

Build the Docker image once:

```powershell
docker build -f Dockerfile.test -t nssan-test .
```

The native workflow uses two Docker volumes to cache the Linux LLVM worktree and build directory:

- `nssan-llvm-src-cache`
- `nssan-llvm-build-cache`

The first native run is expensive because it builds patched Clang. Later runs reuse the cache and are much faster.

## Native Smoke Suite

Run the verified native suite with:

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./run_tests.sh
```

This uses:

- [scripts/build-native-clang.sh](/C:/Users/sanvi/OneDrive/Desktop/cd%20lab/scripts/build-native-clang.sh) to build the patched Clang toolchain
- [scripts/install-native-nssan.sh](/C:/Users/sanvi/OneDrive/Desktop/cd%20lab/scripts/install-native-nssan.sh) to install `NSSanPass.so` and the numerical runtime into Clang's resource directory
- [run_tests.sh](/C:/Users/sanvi/OneDrive/Desktop/cd%20lab/run_tests.sh) to compile and run the 12-case native smoke suite

## Native Demo

Run the evaluator-style demo with:

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./scripts/demo-native.sh
```

The demo sources live in:

- [demo/buggy.c](/C:/Users/sanvi/OneDrive/Desktop/cd%20lab/demo/buggy.c)
- [demo/clean.c](/C:/Users/sanvi/OneDrive/Desktop/cd%20lab/demo/clean.c)

The live command sequence is documented in [demo/DEMO_SCRIPT.md](/C:/Users/sanvi/OneDrive/Desktop/cd%20lab/demo/DEMO_SCRIPT.md).

## Benchmarks

Run the mini benchmark harness with:

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v "C:\llvm-project:/llvm-project" `
  -v nssan-llvm-src-cache:/llvm-src `
  -v nssan-llvm-build-cache:/llvm-build `
  -w /workspace `
  nssan-test bash ./benchmark.sh
```

The benchmark harness compiles and times three programs from [benchmarks/src](/C:/Users/sanvi/OneDrive/Desktop/cd%20lab/benchmarks/src).

## Legacy Wrapper Path

If you already have a host LLVM install and only want the older out-of-tree plugin flow, you can still build the repo directly:

```powershell
cmake -S . -B build -G Ninja -DLLVM_DIR="C:\path\to\lib\cmake\llvm"
cmake --build build
```

Then compile with the wrapper:

```powershell
pwsh ./tools/nssan-clang.ps1 `
  -BuildDir ./build `
  ./tests/smoke/quadratic_near_double_root.c `
  -o ./tests/out/quadratic.exe
```

## Environment Variables

- `NSSAN_THRESHOLD`
  Relative error threshold. Default: `1e-5`.
- `NSSAN_HALT_ON_ERROR`
  If set to a non-zero value, abort after the first report.

## Current Scope

- strongest on scalar `float` code with debug info
- uses higher-precision shadow execution for covered instructions, not full arbitrary-precision math
- does not yet cover heap-aware shadow storage, vectorized code, or general user-defined float-returning functions
- reports once per location/opcode pair to avoid flooding loops
