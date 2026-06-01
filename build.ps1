# build.ps1 — One-command build for NSSan (Windows PowerShell)
# Usage: .\build.ps1
#
# Prerequisites:
#   - Docker Desktop installed and running
#   - LLVM 18.1.8 source cloned at C:\llvm-project

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "       NSSan - Numerical Stability Sanitizer  |  Build" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

# --- Check Docker ---
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: Docker is not installed or not in PATH." -ForegroundColor Red
    Write-Host "Install Docker Desktop from https://www.docker.com/products/docker-desktop/"
    exit 1
}

try {
    docker info 2>&1 | Out-Null
} catch {
    Write-Host "ERROR: Docker daemon is not running. Start Docker Desktop first." -ForegroundColor Red
    exit 1
}

# --- Detect LLVM source ---
$LlvmPath = ""
if (Test-Path "C:\llvm-project\llvm") {
    $LlvmPath = "C:\llvm-project"
} elseif (Test-Path "$env:USERPROFILE\llvm-project\llvm") {
    $LlvmPath = "$env:USERPROFILE\llvm-project"
}

if ([string]::IsNullOrEmpty($LlvmPath)) {
    Write-Host "ERROR: LLVM source not found." -ForegroundColor Red
    Write-Host ""
    Write-Host "Clone it once with:" -ForegroundColor Yellow
    Write-Host '  git clone --depth=1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git C:\llvm-project'
    exit 1
}

Write-Host "Using LLVM source at: $LlvmPath" -ForegroundColor Gray

# --- Step 1: Build Docker image ---
Write-Host ""
Write-Host "[1/3] Building Docker image from Dockerfile.test ..." -ForegroundColor Yellow
docker build -f Dockerfile.test -t nssan-test .
if ($LASTEXITCODE -ne 0) { Write-Host "Docker build failed." -ForegroundColor Red; exit 1 }

# --- Step 2: Build patched Clang + NSSan ---
Write-Host ""
Write-Host "[2/3] Building patched Clang + NSSan inside Docker ..." -ForegroundColor Yellow
Write-Host "      (First run builds LLVM ~15-30 min. Cached after.)" -ForegroundColor Gray
Write-Host ""

docker run --rm `
    -v "${PWD}:/workspace" `
    -v "${LlvmPath}:/llvm-project" `
    -v nssan-llvm-src-cache:/llvm-src `
    -v nssan-llvm-build-cache:/llvm-build `
    -w /workspace `
    nssan-test bash ./scripts/install-native-nssan.sh

if ($LASTEXITCODE -ne 0) { Write-Host "NSSan build failed." -ForegroundColor Red; exit 1 }

# --- Step 3: Verify ---
Write-Host ""
Write-Host "[3/3] Verifying installation ..." -ForegroundColor Yellow

docker run --rm `
    -v "${PWD}:/workspace" `
    -v "${LlvmPath}:/llvm-project" `
    -v nssan-llvm-src-cache:/llvm-src `
    -v nssan-llvm-build-cache:/llvm-build `
    -w /workspace `
    nssan-test bash -c 'echo "int main(){return 0;}" | /llvm-build/bin/clang -fsanitize=numerical -x c -c -o /dev/null -'

if ($LASTEXITCODE -ne 0) { Write-Host "Verification failed." -ForegroundColor Red; exit 1 }

Write-Host ""
Write-Host "Build complete!" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:"
Write-Host "  .\run.ps1          - Run tests, benchmarks, and demo" -ForegroundColor Cyan
Write-Host "  .\run.ps1 tests    - Run 12-case test suite only" -ForegroundColor Cyan
Write-Host "  .\run.ps1 bench    - Run benchmark suite only" -ForegroundColor Cyan
Write-Host "  .\run.ps1 demo     - Run evaluator demo only" -ForegroundColor Cyan
