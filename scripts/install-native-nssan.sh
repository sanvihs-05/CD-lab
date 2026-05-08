#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_BUILD="${LLVM_BUILD:-/llvm-build}"
LLVM_DIR="${LLVM_DIR:-${LLVM_BUILD}/lib/cmake/llvm}"
CLANG_BIN="${CLANG_BIN:-${LLVM_BUILD}/bin/clang}"
CXX_BIN="${CXX_BIN:-${LLVM_BUILD}/bin/clang++}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/native}"

"${ROOT_DIR}/scripts/build-native-clang.sh"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DLLVM_DIR="${LLVM_DIR}" \
  -DCMAKE_C_COMPILER="${CLANG_BIN}" \
  -DCMAKE_CXX_COMPILER="${CXX_BIN}"

cmake --build "${BUILD_DIR}"

RESOURCE_DIR="$("${CLANG_BIN}" -print-resource-dir)"
TARGET_TRIPLE="$("${CLANG_BIN}" -print-target-triple)"
OS_LIB_DIR="${RESOURCE_DIR}/lib/linux"

case "${TARGET_TRIPLE}" in
  x86_64-*)
    ARCH_SUFFIX="x86_64"
    ;;
  i?86-*)
    ARCH_SUFFIX="i386"
    ;;
  *)
    ARCH_SUFFIX="$(uname -m)"
    ;;
esac

install -d "${OS_LIB_DIR}"
install -m 0644 "${BUILD_DIR}/libNSSanRuntime.a" \
  "${OS_LIB_DIR}/libclang_rt.numerical-${ARCH_SUFFIX}.a"
install -m 0755 "${BUILD_DIR}/NSSanPass.so" "${OS_LIB_DIR}/NSSanPass.so"

echo "Installed NSSan artifacts into ${OS_LIB_DIR}"
echo "Custom clang: ${CLANG_BIN}"
