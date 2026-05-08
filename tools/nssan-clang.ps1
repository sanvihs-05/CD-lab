param(
  [string]$Clang = "clang",
  [string]$BuildDir = (Join-Path $PSScriptRoot "..\\build"),
  [string]$PassPlugin,
  [string]$RuntimeLibrary,
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$ClangArgs
)

function Resolve-FirstExistingPath {
  param([string[]]$Candidates)

  foreach ($candidate in $Candidates) {
    if ($candidate -and (Test-Path $candidate)) {
      return (Resolve-Path $candidate).Path
    }
  }

  return $null
}

if (-not $PassPlugin) {
  $PassPlugin = Resolve-FirstExistingPath @(
    (Join-Path $BuildDir "NSSanPass.dll"),
    (Join-Path $BuildDir "NSSanPass.so"),
    (Join-Path $BuildDir "NSSanPass.dylib")
  )
}

if (-not $RuntimeLibrary) {
  $RuntimeLibrary = Resolve-FirstExistingPath @(
    (Join-Path $BuildDir "NSSanRuntime.lib"),
    (Join-Path $BuildDir "libNSSanRuntime.a")
  )
}

if (-not $PassPlugin) {
  throw "NSSanPass plugin was not found. Build the project first or pass -PassPlugin explicitly."
}

if (-not $RuntimeLibrary) {
  throw "NSSanRuntime library was not found. Build the project first or pass -RuntimeLibrary explicitly."
}

$command = @(
  $Clang
  "-g"
  "-O0"
  "-fpass-plugin=$PassPlugin"
) + $ClangArgs + @($RuntimeLibrary)

if (-not $IsWindows) {
  $command += "-lm"
  $command += "-lstdc++"
  $command += "-pthread"
}

Write-Host ($command -join " ")
if ($IsWindows) {
  & $Clang "-g" "-O0" "-fpass-plugin=$PassPlugin" @ClangArgs $RuntimeLibrary
} else {
  & $Clang "-g" "-O0" "-fpass-plugin=$PassPlugin" @ClangArgs $RuntimeLibrary "-lm" "-lstdc++" "-pthread"
}
exit $LASTEXITCODE
