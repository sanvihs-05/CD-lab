#!/usr/bin/env bash
# run.sh — One-command runner for NSSan tests, benchmarks, and demo
# Usage:
#   ./run.sh           — Run everything (tests + benchmarks + demo)
#   ./run.sh tests     — Run 12-case test suite only
#   ./run.sh bench     — Run benchmark suite only
#   ./run.sh demo      — Run evaluator demo only
#
# Prerequisites:
#   - Docker installed and running
#   - ./build.sh completed successfully (or will be triggered automatically)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

green="$(printf '\033[1;32m')"
red="$(printf '\033[1;31m')"
cyan="$(printf '\033[1;36m')"
reset="$(printf '\033[0m')"

# --- Detect LLVM source ---
LLVM_HOST_PATH=""
if [[ -d "/llvm-project/llvm" ]]; then
  LLVM_HOST_PATH="/llvm-project"
elif [[ -d "C:/llvm-project/llvm" ]]; then
  LLVM_HOST_PATH="C:/llvm-project"
elif [[ -d "${HOME}/llvm-project/llvm" ]]; then
  LLVM_HOST_PATH="${HOME}/llvm-project"
fi

if [[ -z "${LLVM_HOST_PATH}" ]]; then
  printf '%sERROR%s: LLVM source not found. Run ./build.sh first.\n' "${red}" "${reset}"
  exit 1
fi

DOCKER_CMD=(
  docker run --rm
  -v "${ROOT_DIR}:/workspace"
  -v "${LLVM_HOST_PATH}:/llvm-project"
  -v nssan-llvm-src-cache:/llvm-src
  -v nssan-llvm-build-cache:/llvm-build
  -w /workspace
  nssan-test
)

run_tests() {
  echo "╔════════════════════════════════════════════════════╗"
  echo "║            NSSan — Test Suite (12 cases)          ║"
  echo "╚════════════════════════════════════════════════════╝"
  echo
  "${DOCKER_CMD[@]}" bash ./run_tests.sh
  echo
}

run_bench() {
  echo "╔════════════════════════════════════════════════════╗"
  echo "║          NSSan — Benchmark Suite (3 programs)     ║"
  echo "╚════════════════════════════════════════════════════╝"
  echo
  "${DOCKER_CMD[@]}" bash ./benchmark.sh
  echo
}

run_demo() {
  echo "╔════════════════════════════════════════════════════╗"
  echo "║             NSSan — Evaluator Demo                ║"
  echo "╚════════════════════════════════════════════════════╝"
  echo
  echo "Demo shows three steps:"
  echo "  1. Compile and run buggy.c WITHOUT NSSan  → silent wrong result"
  echo "  2. Compile and run buggy.c WITH NSSan     → error report (FAILURE CASE)"
  echo "  3. Compile and run clean.c WITH NSSan     → no report (WORKING CASE)"
  echo
  "${DOCKER_CMD[@]}" bash ./scripts/demo-native.sh
  echo
}

MODE="${1:-all}"

case "${MODE}" in
  tests|test)
    run_tests
    ;;
  bench|benchmark)
    run_bench
    ;;
  demo)
    run_demo
    ;;
  all)
    run_tests
    echo "────────────────────────────────────────────────────"
    echo
    run_bench
    echo "────────────────────────────────────────────────────"
    echo
    run_demo
    printf '%s✓ All runs complete!%s\n' "${green}" "${reset}"
    ;;
  *)
    echo "Usage: ./run.sh [tests|bench|demo|all]"
    exit 1
    ;;
esac
