param(
  [string]$BuildDir = (Join-Path $PSScriptRoot "..\\build"),
  [string]$Clang = "clang",
  [string]$Threshold = "1e-5"
)

$runner = Join-Path $PSScriptRoot "..\\tools\\nssan-clang.ps1"
$outDir = Join-Path $PSScriptRoot "out"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$cases = @(
  @{ Name = "quadratic_near_double_root"; ExpectReport = $true; Threshold = $Threshold },
  @{ Name = "naive_sum_1e7"; ExpectReport = $true; Threshold = $Threshold },
  @{ Name = "kahan_sum_1e7"; ExpectReport = $false; Threshold = $Threshold },
  @{ Name = "dot_product_float"; ExpectReport = $true; Threshold = $Threshold },
  @{ Name = "exp_series_overflow"; ExpectReport = $true; Threshold = $Threshold },
  @{ Name = "sqrt_negative_eps"; ExpectReport = $true; Threshold = $Threshold },
  @{ Name = "horner_vs_naive_poly"; ExpectReport = $true; Threshold = $Threshold },
  @{ Name = "matrix_ill_cond"; ExpectReport = $true; Threshold = $Threshold },
  @{ Name = "stokes_euler"; ExpectReport = $true; Threshold = $Threshold },
  @{ Name = "no_cancel_baseline"; ExpectReport = $false; Threshold = $Threshold },
  @{ Name = "fnmadd_fused"; ExpectReport = $true; Threshold = "1e-8" },
  @{ Name = "catastrophic_1minus_cos"; ExpectReport = $true; Threshold = $Threshold }
)

$failures = @()

foreach ($case in $cases) {
  $source = Join-Path $PSScriptRoot "smoke\\$($case.Name).c"
  $binary = Join-Path $outDir "$($case.Name).exe"

  & $runner -Clang $Clang -BuildDir $BuildDir $source "-o" $binary
  if ($LASTEXITCODE -ne 0) {
    $failures += "$($case.Name): compile failed"
    continue
  }

  $stderrFile = Join-Path $outDir "$($case.Name).stderr.txt"
  $stdoutFile = Join-Path $outDir "$($case.Name).stdout.txt"

  $oldThreshold = $env:NSSAN_THRESHOLD
  $env:NSSAN_THRESHOLD = $case.Threshold

  try {
    & $binary 1> $stdoutFile 2> $stderrFile
  } finally {
    if ($null -eq $oldThreshold) {
      Remove-Item Env:\NSSAN_THRESHOLD -ErrorAction SilentlyContinue
    } else {
      $env:NSSAN_THRESHOLD = $oldThreshold
    }
  }

  $stderr = ""
  if (Test-Path $stderrFile) {
    $stderr = Get-Content $stderrFile -Raw
  }

  $reported = $stderr -match "NUMERICAL SANITIZER: ERROR"

  if ($case.ExpectReport -and -not $reported) {
    $failures += "$($case.Name): expected a report but saw none"
  }

  if (-not $case.ExpectReport -and $reported) {
    $failures += "$($case.Name): expected no report but saw one"
  }
}

if ($failures.Count -gt 0) {
  Write-Host "NSSan smoke suite failed:"
  $failures | ForEach-Object { Write-Host "  $_" }
  exit 1
}

Write-Host "NSSan smoke suite passed."
