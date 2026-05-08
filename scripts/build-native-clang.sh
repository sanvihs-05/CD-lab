#!/usr/bin/env bash
set -euo pipefail

LLVM_SRC="${LLVM_SRC:-/llvm-project}"
LLVM_WORKTREE="${LLVM_WORKTREE:-/llvm-src}"
LLVM_BUILD="${LLVM_BUILD:-/llvm-build}"
BOOTSTRAP_CC="${BOOTSTRAP_CC:-clang-18}"
BOOTSTRAP_CXX="${BOOTSTRAP_CXX:-clang++-18}"

if [[ ! -d "${LLVM_SRC}/llvm" ]]; then
  echo "LLVM source tree not found at ${LLVM_SRC}." >&2
  echo "Mount your host checkout into /llvm-project before running this script." >&2
  exit 1
fi

if [[ ! -d "${LLVM_WORKTREE}/llvm" ]]; then
  mkdir -p "${LLVM_WORKTREE}"
  find "${LLVM_WORKTREE}" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
  git clone "${LLVM_SRC}" "${LLVM_WORKTREE}"
fi

mkdir -p "${LLVM_BUILD}"

PATCH_FILE="$(mktemp)"
git -C "${LLVM_SRC}" diff --binary -- \
  clang/include/clang/Basic/Sanitizers.def \
  clang/include/clang/Driver/SanitizerArgs.h \
  clang/lib/Driver/SanitizerArgs.cpp \
  clang/lib/Driver/ToolChains/CommonArgs.cpp \
  clang/lib/Driver/ToolChains/Linux.cpp > "${PATCH_FILE}"

SOURCE_REV="$(git -C "${LLVM_SRC}" rev-parse HEAD)"
PATCH_HASH="$(sha256sum "${PATCH_FILE}" | awk '{print $1}')"
BUILD_SIGNATURE="${SOURCE_REV}:${PATCH_HASH}"
PATCH_STAMP="${LLVM_BUILD}/.nssan_patch.sig"
BUILD_STAMP="${LLVM_BUILD}/.nssan_build.sig"
PREVIOUS_PATCH_SIGNATURE=""
if [[ -f "${PATCH_STAMP}" ]]; then
  PREVIOUS_PATCH_SIGNATURE="$(<"${PATCH_STAMP}")"
fi

cache_uses_shared_dylib() {
  [[ -f "${LLVM_BUILD}/CMakeCache.txt" ]] && \
    grep -q '^LLVM_BUILD_LLVM_DYLIB:BOOL=ON$' "${LLVM_BUILD}/CMakeCache.txt" && \
    grep -q '^LLVM_LINK_LLVM_DYLIB:BOOL=ON$' "${LLVM_BUILD}/CMakeCache.txt"
}

native_clang_artifacts_ready() {
  [[ -x "${LLVM_BUILD}/bin/clang" ]] &&
    [[ -x "${LLVM_BUILD}/bin/llvm-ar" ]] &&
    [[ -e "${LLVM_BUILD}/lib/libLLVM.so" ]]
}

native_clang_supports_numerical() {
  native_clang_artifacts_ready || return 1
  "${LLVM_BUILD}/bin/clang" -### -x c -fsanitize=numerical -c - >/dev/null 2>&1 <<'EOF'
int main(void) { return 0; }
EOF
}

native_clang_cache_matches_signature() {
  [[ -f "${PATCH_STAMP}" ]] &&
    [[ "$(<"${PATCH_STAMP}")" == "${BUILD_SIGNATURE}" ]] &&
    cache_uses_shared_dylib &&
    native_clang_supports_numerical
}

native_clang_is_current() {
  [[ -f "${BUILD_STAMP}" ]] &&
    [[ "$(<"${BUILD_STAMP}")" == "${BUILD_SIGNATURE}" ]] &&
    native_clang_cache_matches_signature
}

if [[ "${BUILD_SIGNATURE}" != "${PREVIOUS_PATCH_SIGNATURE}" ]]; then
  git -C "${LLVM_WORKTREE}" fetch --depth=1 "${LLVM_SRC}" "${SOURCE_REV}" >/dev/null
  git -C "${LLVM_WORKTREE}" reset --hard FETCH_HEAD >/dev/null
  git -C "${LLVM_WORKTREE}" clean -fd >/dev/null
  if [[ -s "${PATCH_FILE}" ]]; then
    git -C "${LLVM_WORKTREE}" apply --whitespace=nowarn "${PATCH_FILE}"
  fi
  printf '%s' "${BUILD_SIGNATURE}" > "${PATCH_STAMP}"
  rm -f "${BUILD_STAMP}"
fi

rm -f "${PATCH_FILE}"

if [[ -f "${LLVM_BUILD}/CMakeCache.txt" ]] && \
   ! grep -q '^LLVM_BUILD_LLVM_DYLIB:BOOL=ON$' "${LLVM_BUILD}/CMakeCache.txt"; then
  find "${LLVM_BUILD}" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
fi

if [[ ! -f "${BUILD_STAMP}" ]] && native_clang_cache_matches_signature; then
  printf '%s' "${BUILD_SIGNATURE}" > "${BUILD_STAMP}"
  echo "Adopted existing native clang cache at ${LLVM_BUILD}"
  exit 0
fi

if native_clang_is_current; then
  echo "Native clang cache is up to date at ${LLVM_BUILD}"
  exit 0
fi

if ! command -v "${BOOTSTRAP_CC}" >/dev/null 2>&1; then
  BOOTSTRAP_CC="cc"
fi

if ! command -v "${BOOTSTRAP_CXX}" >/dev/null 2>&1; then
  BOOTSTRAP_CXX="c++"
fi

cmake -S "${LLVM_WORKTREE}/llvm" -B "${LLVM_BUILD}" -G Ninja \
  -DLLVM_ENABLE_PROJECTS="clang" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_TARGETS_TO_BUILD=X86 \
  -DLLVM_BUILD_LLVM_DYLIB=ON \
  -DLLVM_LINK_LLVM_DYLIB=ON \
  -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
  -DCLANG_ENABLE_ARCMT=OFF \
  -DCLANG_ENABLE_Z3_SOLVER=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DCMAKE_C_COMPILER="${BOOTSTRAP_CC}" \
  -DCMAKE_CXX_COMPILER="${BOOTSTRAP_CXX}"

cmake --build "${LLVM_BUILD}" --target clang clang-resource-headers llvm-ar
printf '%s' "${BUILD_SIGNATURE}" > "${BUILD_STAMP}"
echo "Built patched clang at ${LLVM_BUILD}"
