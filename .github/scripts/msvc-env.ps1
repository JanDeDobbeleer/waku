# Loads the MSVC build environment for the rest of a GitHub Actions job (what vcvars does in a
# console). Replaces a third-party action that still runs on Node.js 20.
param(
  [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Arch
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$root = & $vswhere -latest -products * -property installationPath
if (-not $root) { throw 'Visual Studio with the C++ tools was not found' }

$script = if ($Arch -eq 'x64') { 'vcvars64.bat' } else { 'vcvarsarm64.bat' }
$bat = Join-Path $root (Join-Path 'VC\Auxiliary\Build' $script)
if (-not (Test-Path -LiteralPath $bat)) { throw "$bat does not exist" }

$before = @{}
Get-ChildItem Env: | ForEach-Object { $before[$_.Name] = $_.Value }

$dump = cmd /c "`"$bat`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw "$script failed" }

foreach ($line in $dump) {
  $i = $line.IndexOf('=')
  if ($i -le 0) { continue }
  $name = $line.Substring(0, $i)
  $value = $line.Substring($i + 1)
  if ($before[$name] -eq $value) { continue }
  if ($name -eq 'Path') {
    $known = @($before['Path'] -split ';')
    # GITHUB_PATH prepends each line in turn, so write the new entries last to first to keep
    # the order vcvars chose (the MSVC bin folder before the bundled LLVM).
    $new = @($value -split ';' | Where-Object { $_ -and ($known -notcontains $_) })
    [array]::Reverse($new)
    $new | ForEach-Object { $_ >> $env:GITHUB_PATH }
  } elseif ($name -ne 'VCPKG_ROOT') {
    # VCPKG_ROOT stays what the workflow sets: the vcpkg checked out at the manifest baseline.
    "$name=$value" >> $env:GITHUB_ENV
  }
}

# Visual Studio 2026 also puts clang on PATH. waku builds with MSVC only.
"CC=cl" >> $env:GITHUB_ENV
"CXX=cl" >> $env:GITHUB_ENV
