# run.ps1 — One-command runner for NSSan (Windows PowerShell)
# Usage:
#   .\run.ps1           — Run everything (tests + benchmarks + demo)
#   .\run.ps1 tests     — Run 12-case test suite only
#   .\run.ps1 bench     — Run benchmark suite only
#   .\run.ps1 demo      — Run evaluator demo only

param(
    [string]$Mode = "all"
)

$ErrorActionPreference = "Stop"

# --- Detect LLVM source ---
$LlvmPath = ""
if (Test-Path "C:\llvm-project\llvm") {
    $LlvmPath = "C:\llvm-project"
} elseif (Test-Path "$env:USERPROFILE\llvm-project\llvm") {
    $LlvmPath = "$env:USERPROFILE\llvm-project"
}

if ([string]::IsNullOrEmpty($LlvmPath)) {
    Write-Host "ERROR: LLVM source not found. Run .\build.ps1 first." -ForegroundColor Red
    exit 1
}

function Invoke-Docker {
    param([string]$Script)
    docker run --rm `
        -v "${PWD}:/workspace" `
        -v "${LlvmPath}:/llvm-project" `
        -v nssan-llvm-src-cache:/llvm-src `
        -v nssan-llvm-build-cache:/llvm-build `
        -w /workspace `
        nssan-test bash $Script
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Command failed with exit code $LASTEXITCODE" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

function Run-Tests {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "            NSSan - Test Suite (12 cases)" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host ""
    Invoke-Docker "./run_tests.sh"
    Write-Host ""
}

function Run-Bench {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "          NSSan - Benchmark Suite (3 programs)" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host ""
    Invoke-Docker "./benchmark.sh"
    Write-Host ""
}

function Run-Demo {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "             NSSan - Evaluator Demo" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Demo shows three steps:" -ForegroundColor Gray
    Write-Host "  1. Compile and run buggy.c WITHOUT NSSan  -> silent wrong result" -ForegroundColor Gray
    Write-Host "  2. Compile and run buggy.c WITH NSSan     -> error report (FAILURE CASE)" -ForegroundColor Gray
    Write-Host "  3. Compile and run clean.c WITH NSSan     -> no report (WORKING CASE)" -ForegroundColor Gray
    Write-Host ""
    Invoke-Docker "./scripts/demo-native.sh"
    Write-Host ""
}

switch ($Mode.ToLower()) {
    "tests" { Run-Tests }
    "test"  { Run-Tests }
    "bench" { Run-Bench }
    "benchmark" { Run-Bench }
    "demo"  { Run-Demo }
    "all" {
        Run-Tests
        Write-Host "----------------------------------------------------------------" -ForegroundColor DarkGray
        Run-Bench
        Write-Host "----------------------------------------------------------------" -ForegroundColor DarkGray
        Run-Demo
        Write-Host "All runs complete!" -ForegroundColor Green
    }
    default {
        Write-Host "Usage: .\run.ps1 [tests|bench|demo|all]"
        exit 1
    }
}
