# live.ps1 — Launch the NSSan live playground (local web UI backed by the real toolchain).
# Usage:  .\demo\live.ps1        (from the project root)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot   # project root (parent of demo/)
$Port = 7890

# --- locate LLVM source (same logic as run.ps1) ---
$LlvmPath = ""
if (Test-Path "C:\llvm-project\llvm") { $LlvmPath = "C:\llvm-project" }
elseif (Test-Path "$env:USERPROFILE\llvm-project\llvm") { $LlvmPath = "$env:USERPROFILE\llvm-project" }
if ([string]::IsNullOrEmpty($LlvmPath)) {
    Write-Host "ERROR: LLVM source not found. Run .\build.ps1 first." -ForegroundColor Red
    exit 1
}

# --- check Docker is up ---
# Suppress docker's stderr warnings at the cmd level so they don't trip
# PowerShell's Stop preference; rely on the real exit code.
cmd /c "docker info >NUL 2>NUL"
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Docker is not running. Start Docker Desktop, wait ~30s, then retry." -ForegroundColor Red
    exit 1
}

# --- check node ---
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: Node.js not found on PATH." -ForegroundColor Red
    exit 1
}

$env:WORKSPACE = $Root
$env:LLVM_PATH = $LlvmPath
$env:NSSAN_PORT = "$Port"

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "             NSSan - Live Playground" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Opening http://localhost:$Port  (first run warms the toolchain)" -ForegroundColor Gray
Write-Host "Press Ctrl+C in this window to stop the server." -ForegroundColor Gray
Write-Host ""

# open the browser shortly after the server starts
Start-Job -ScriptBlock { Start-Sleep -Seconds 2; Start-Process "http://localhost:$using:Port" } | Out-Null

node "$Root\demo\live-server.js"
