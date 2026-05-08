#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
CLANG="${CLANG:-clang-18}"
LLVM_DIR="${LLVM_DIR:-/usr/lib/llvm-18/lib/cmake/llvm}"
THRESHOLD="${NSSAN_THRESHOLD:-1e-5}"

cmake -S . -B "${BUILD_DIR}" -G Ninja \
  -DLLVM_DIR="${LLVM_DIR}" \
  -DCMAKE_C_COMPILER="${CLANG}" \
  -DCMAKE_CXX_COMPILER="clang++-18"

cmake --build "${BUILD_DIR}"

mkdir -p tests/out

cases=(
  quadratic_near_double_root:report:${THRESHOLD}
  naive_sum_1e7:report:${THRESHOLD}
  kahan_sum_1e7:clean:${THRESHOLD}
  dot_product_float:report:${THRESHOLD}
  exp_series_overflow:report:${THRESHOLD}
  sqrt_negative_eps:report:${THRESHOLD}
  horner_vs_naive_poly:report:${THRESHOLD}
  matrix_ill_cond:report:${THRESHOLD}
  stokes_euler:report:${THRESHOLD}
  no_cancel_baseline:clean:${THRESHOLD}
  fnmadd_fused:report:1e-8
  catastrophic_1minus_cos:report:${THRESHOLD}
)

failures=0

for entry in "${cases[@]}"; do
  name="${entry%%:*}"
  rest="${entry#*:}"
  mode="${rest%%:*}"
  case_threshold="${entry##*:}"
  src="tests/smoke/${name}.c"
  bin="tests/out/${name}"
  stderr_file="tests/out/${name}.stderr.txt"
  stdout_file="tests/out/${name}.stdout.txt"

  "${CLANG}" -g -O0 -fpass-plugin="${BUILD_DIR}/NSSanPass.so" \
    "${src}" -o "${bin}" "${BUILD_DIR}/libNSSanRuntime.a" -lm -lstdc++ -pthread

  NSSAN_THRESHOLD="${case_threshold}" "${bin}" >"${stdout_file}" 2>"${stderr_file}" || true

  if grep -q "NUMERICAL SANITIZER: ERROR" "${stderr_file}"; then
    reported=1
  else
    reported=0
  fi

  if [[ "${mode}" == "report" && "${reported}" -eq 0 ]]; then
    echo "FAIL ${name}: expected a report but saw none"
    failures=$((failures + 1))
  fi

  if [[ "${mode}" == "clean" && "${reported}" -eq 1 ]]; then
    echo "FAIL ${name}: expected no report but saw one"
    failures=$((failures + 1))
  fi
done

if [[ "${failures}" -ne 0 ]]; then
  echo "Smoke suite failed with ${failures} failing case(s)."
  exit 1
fi

echo "Smoke suite passed."
