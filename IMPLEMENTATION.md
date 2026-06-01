# NSSan — Implementation Details

## Architecture Overview

NSSan comprises three components:

```
┌────────────────┐     ┌──────────────────┐     ┌──────────────────────┐
│  Clang Driver  │────▶│  NSSanPass.so    │────▶│  NSSanRuntime.a      │
│  (5 patches)   │     │  (LLVM IR pass)  │     │  (check + report)    │
└────────────────┘     └──────────────────┘     └──────────────────────┘
  -fsanitize=            Instruments float        Compares float vs
  numerical              ops with shadow          double at runtime
                         double ops
```

| Component | File | Language | Lines |
|-----------|------|----------|-------|
| LLVM Pass | `lib/NSSanPass.cpp` | C++17 | ~460 |
| Runtime Library | `runtime/NSSanRuntime.cpp` | C++17 | ~180 |
| Runtime Header | `include/nssan/NSSanRuntime.h` | C++ | ~19 |
| Build System | `CMakeLists.txt` | CMake | ~92 |

## LLVM Pass — `NSSanPass.cpp`

### Pass Registration

NSSan registers as a **function pass** using the new LLVM pass manager:

```cpp
class NSSanFunctionPass : public PassInfoMixin<NSSanFunctionPass> {
public:
  static bool isRequired() { return true; }
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &);
};
```

The pass is registered via `llvmGetPassPluginInfo()` with two callbacks:

1. **Pipeline parsing**: responds to `-passes=nssan` for manual invocation
2. **Pipeline start EP**: automatically runs at the start of the optimization pipeline when loaded

### Shadow Map

The core data structure is a `DenseMap<Value *, Value *>` called `ShadowMap`. It maps each LLVM `float` value to its corresponding `double` shadow:

```cpp
using ShadowMap = DenseMap<Value *, Value *>;
```

Shadow values are created on demand via `getOrCreateShadow()`:

- **Constants**: `ConstantFP` → convert to `double` via `convertToDouble()`
- **Undef values**: remain `UndefValue` in `double` type
- **Function arguments**: `fpext float → double` inserted at function entry
- **Other values**: fallback `fpext` at current insertion point

### Instrumented Instructions

The pass iterates over all instructions in a function via `make_early_inc_range(instructions(F))` and instruments four categories:

#### 1. Binary Float Operations (`fadd`, `fsub`, `fmul`, `fdiv`)

```
Original:  %result = fadd float %a, %b
Inserted:  %shadow_a = ...  (from ShadowMap or fpext)
           %shadow_b = ...
           %shadow_result = fadd double %shadow_a, %shadow_b
           call void @__nssan_check_float_op(
               float %result, double %shadow_result,
               i8* @file_str, i32 line, i32 col, i8* @"fadd")
```

The shadow result is stored in `ShadowMap` for downstream use.

#### 2. Load Instructions

When a `float` is loaded from memory, the runtime's shadow store is queried:

```
Original:  %val = load float, ptr %addr
Inserted:  %shadow = call double @__nssan_shadow_load(ptr %addr, float %val)
```

The runtime returns the tracked shadow if one exists for that address, or `fpext(current)` as fallback.

#### 3. Store Instructions

When a `float` is stored to memory, the corresponding shadow is recorded:

```
Original:  store float %val, ptr %addr
Inserted:  %shadow = ...  (from ShadowMap)
           call void @__nssan_shadow_store(ptr %addr, double %shadow)
```

#### 4. Math Calls and Intrinsics

The pass recognizes both library calls and LLVM intrinsics:

| Float Function | Shadow Replacement | Category |
|---|---|---|
| `sqrtf` / `llvm.sqrt.f32` | `sqrt` / `llvm.sqrt.f64` | Unary |
| `sinf` / `llvm.sin.f32` | `sin` / `llvm.sin.f64` | Unary |
| `cosf` / `llvm.cos.f32` | `cos` / `llvm.cos.f64` | Unary |
| `expf` / `llvm.exp.f32` | `exp` / `llvm.exp.f64` | Unary |
| `logf` / `llvm.log.f32` | `log` / `llvm.log.f64` | Unary |
| `fabsf` / `llvm.fabs.f32` | `llvm.fabs.f64` | Unary |
| `fmaf` / `llvm.fma.f32` / `llvm.fmuladd.f32` | `fma` / `llvm.fma.f64` | Ternary |

For intrinsics, `Intrinsic::getDeclaration(&M, Id, {DoubleTy})` generates the double-precision overload. For library calls, the pass looks up the double-precision equivalent by name (e.g., `sqrtf` → `sqrt`).

### Debug Info and Source Locations

Each check call includes source location extracted from LLVM debug metadata:

```cpp
DILocation *Loc = Instruction.getDebugLoc().get();
// → file path, line number, column number
```

The `locationPath()` helper constructs the full file path from `DIScope` and `DIFile`. If debug info is absent, `<unknown>:0:0` is used.

## Runtime Library — `NSSanRuntime.cpp`

### Runtime State

A singleton `RuntimeState` manages:

```cpp
struct RuntimeState {
  std::mutex lock;                                    // Thread safety
  std::unordered_map<std::uintptr_t, double> shadows; // Address → shadow
  std::unordered_set<std::string> reported_locations;  // Deduplication
  double threshold = 1.0e-5;                          // Configurable
  bool halt_on_error = false;                         // Optional abort
};
```

Initialized once via `std::call_once`, reading environment variables `NSSAN_THRESHOLD` and `NSSAN_HALT_ON_ERROR`.

### Exported Functions

#### `__nssan_shadow_store(const void *addr, double shadow)`

Records a shadow value for a memory address. Thread-safe via `std::lock_guard`.

#### `__nssan_shadow_load(const void *addr, float current) → double`

Returns the tracked shadow for `addr` if available, otherwise promotes `current` to `double`.

#### `__nssan_check_float_op(float result, double shadow, const char *file, uint32_t line, uint32_t col, const char *op_name)`

The core check function:

1. **Non-finite check**: If either value is NaN or Inf, report immediately
2. **Absolute tolerance filter**: Skip if `|float − shadow| ≤ max(threshold², 1e-12)` — avoids noise near zero
3. **Relative error**: Compute `|float − shadow| / max(|shadow|, abs_tolerance)`
4. **Report if exceeds threshold**: Emit diagnostic with issue type classification

### Issue Classification

The runtime classifies detected issues:

| Condition | Label |
|-----------|-------|
| NaN in result or shadow | `NaN propagation` |
| Inf in result or shadow | `Infinity propagation` |
| Subtraction operation (`fsub`) | `Catastrophic Cancellation` |
| Other divergence | `Numerical Divergence` |

### Report Format

```
=== NUMERICAL SANITIZER: ERROR ===
  Type:     Catastrophic Cancellation
  Location: ./demo/buggy.c:6:20
  Operation: fsub
  float:    4.62532043e-08
  shadow:   4.9999999583255429e-08
  error:    7.4899279e-02x threshold exceeded
```

Reports are **deduplicated** by `(file, line, column, opcode)` key — each unique site is reported at most once.

## Clang Driver Integration

Five files in the Clang source tree are patched to support `-fsanitize=numerical`:

### 1. `clang/include/clang/Basic/Sanitizers.def`

Registers `numerical` as a new sanitizer kind:

```cpp
SANITIZER("numerical", Numerical)
```

### 2. `clang/include/clang/Driver/SanitizerArgs.h`

Adds `NeedsNumericalRt` flag to track whether the numerical runtime library should be linked.

### 3. `clang/lib/Driver/SanitizerArgs.cpp`

Enables the `Numerical` sanitizer group and sets `NeedsNumericalRt = true` when `-fsanitize=numerical` is passed.

### 4. `clang/lib/Driver/ToolChains/CommonArgs.cpp`

Adds the NSSan pass plugin to the compiler invocation:
- `-fplugin=<resource-dir>/lib/linux/NSSanPass.so`
- `-g` (ensures debug info for source locations)

Links the runtime library:
- `-lclang_rt.numerical-<arch>`

### 5. `clang/lib/Driver/ToolChains/Linux.cpp`

Adds the resource directory library path so the linker can find `libclang_rt.numerical-x86_64.a`.

## Build System — `CMakeLists.txt`

### Pass Plugin

```cmake
if(COMMAND add_llvm_pass_plugin)
  add_llvm_pass_plugin(NSSanPass lib/NSSanPass.cpp)
else()
  add_library(NSSanPass MODULE lib/NSSanPass.cpp)
endif()
```

Uses `add_llvm_pass_plugin` if available (in-tree builds), otherwise falls back to a plain `MODULE` library. Built with `-fno-rtti` to match LLVM's build conventions.

### Runtime Library

```cmake
add_library(NSSanRuntime STATIC runtime/NSSanRuntime.cpp)
```

Built as a static archive. Compiled with `-fno-sanitize=all` to prevent recursive instrumentation.

### Installation

The `scripts/install-native-nssan.sh` script installs artifacts into Clang's resource directory:

- `NSSanPass.so` → `<resource-dir>/lib/linux/NSSanPass.so`
- `libNSSanRuntime.a` → `<resource-dir>/lib/linux/libclang_rt.numerical-x86_64.a`

This placement allows the Clang driver to find them automatically with `-fsanitize=numerical`.
