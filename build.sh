#!/usr/bin/env bash
# build.sh — One-command build for NSSan
# Usage: ./build.sh
#
# Prerequisites:
#   - Docker installed and running
#   - LLVM 18.1.8 source cloned at C:\llvm-project (Windows) or /llvm-project (Linux)
#
# This script:
#   1. Builds the Docker image from Dockerfile.test
#   2. Builds the patched Clang toolchain with -fsanitize=numerical support
#   3. Compiles NSSanPass.so and NSSanRuntime.a
#   4. Installs them into Clang's resource directory

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

green="$(printf '\033[1;32m')"
red="$(printf '\033[1;31m')"
yellow="$(printf '\033[1;33m')"
reset="$(printf '\033[0m')"

echo "╔════════════════════════════════════════════════════╗"
echo "║       NSSan — Numerical Stability Sanitizer       ║"
echo "║                   Build Script                    ║"
echo "╚════════════════════════════════════════════════════╝"
echo

# --- Check Docker ---
if ! command -v docker &>/dev/null; then
  printf '%sERROR%s: Docker is not installed or not in PATH.\n' "${red}" "${reset}"
  echo "Install Docker Desktop from https://www.docker.com/products/docker-desktop/"
  exit 1
fi

if ! docker info &>/dev/null 2>&1; then
  printf '%sERROR%s: Docker daemon is not running. Start Docker Desktop first.\n' "${red}" "${reset}"
  exit 1
fi

echo "[1/3] Building Docker image from Dockerfile.test ..."
docker build -f "${ROOT_DIR}/Dockerfile.test" -t nssan-test "${ROOT_DIR}"
echo

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
  printf '%sWARNING%s: LLVM source not found at standard locations.\n' "${yellow}" "${reset}"
  echo "Clone it once with:"
  echo "  git clone --depth=1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git C:\\llvm-project"
  echo
  echo "Or set LLVM_HOST_PATH to your clone location."
  exit 1
fi

echo "[2/3] Building patched Clang + NSSan inside Docker ..."
echo "      (First run builds LLVM — this can take 15–30 minutes.)"
echo "      (Subsequent runs reuse cached Docker volumes.)"
echo

docker run --rm \
  -v "${ROOT_DIR}:/workspace" \
  -v "${LLVM_HOST_PATH}:/llvm-project" \
  -v nssan-llvm-src-cache:/llvm-src \
  -v nssan-llvm-build-cache:/llvm-build \
  -w /workspace \
  nssan-test bash ./scripts/install-native-nssan.sh

echo
echo "[3/3] Verifying installation ..."

docker run --rm \
  -v "${ROOT_DIR}:/workspace" \
  -v "${LLVM_HOST_PATH}:/llvm-project" \
  -v nssan-llvm-src-cache:/llvm-src \
  -v nssan-llvm-build-cache:/llvm-build \
  -w /workspace \
  nssan-test /llvm-build/bin/clang -fsanitize=numerical -x c -c -o /dev/null - <<'EOF'
int main(void) { return 0; }
EOF

echo
printf '%s✓ Build complete!%s\n' "${green}" "${reset}"
echo
echo "Next steps:"
echo "  ./run.sh          — Run tests, benchmarks, and demo"
echo "  ./run.sh tests    — Run 12-case test suite only"
echo "  ./run.sh bench    — Run benchmark suite only"
echo "  ./run.sh demo     — Run evaluator demo only"
