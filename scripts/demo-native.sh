#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLANG_BIN="${CLANG_BIN:-/llvm-build/bin/clang}"
OUT_DIR="${ROOT_DIR}/demo/out"
mkdir -p "${OUT_DIR}"

show_cmd() {
  printf '$'
  for arg in "$@"; do
    printf ' %q' "${arg}"
  done
  printf '\n'
}

echo "Preparing native NSSan toolchain"
"${ROOT_DIR}/scripts/install-native-nssan.sh"
echo

echo "1. Compile normally"
show_cmd "${CLANG_BIN}" -g "${ROOT_DIR}/demo/buggy.c" -lm -o "${OUT_DIR}/buggy"
"${CLANG_BIN}" -g "${ROOT_DIR}/demo/buggy.c" -lm -o "${OUT_DIR}/buggy"
show_cmd "${OUT_DIR}/buggy"
"${OUT_DIR}/buggy"
echo

echo "2. Compile with NSSan"
show_cmd "${CLANG_BIN}" -g -fsanitize=numerical "${ROOT_DIR}/demo/buggy.c" -lm -o "${OUT_DIR}/buggy_san"
"${CLANG_BIN}" -g -fsanitize=numerical "${ROOT_DIR}/demo/buggy.c" -lm -o "${OUT_DIR}/buggy_san"
show_cmd "${OUT_DIR}/buggy_san"
"${OUT_DIR}/buggy_san" 2>&1
echo

echo "3. Clean baseline"
show_cmd "${CLANG_BIN}" -g -fsanitize=numerical "${ROOT_DIR}/demo/clean.c" -o "${OUT_DIR}/clean_san"
"${CLANG_BIN}" -g -fsanitize=numerical "${ROOT_DIR}/demo/clean.c" -o "${OUT_DIR}/clean_san"
show_cmd "${OUT_DIR}/clean_san"
# NSSan now prints its own end-of-run verdict (=== NSSan SUMMARY ===),
# so we just surface the tool's real output instead of faking a message.
"${OUT_DIR}/clean_san" 2>&1
