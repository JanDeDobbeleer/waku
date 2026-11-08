# Installs a pinned ccache and points CMake at it for the rest of the job.
param(
  [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Arch
)

$ErrorActionPreference = 'Stop'
$version = '4.14.2'
$flavor = if ($Arch -eq 'x64') { 'x86_64' } else { 'aarch64' }
$sha256 = @{
  x86_64  = '6993ce645e53192cec592ee23186200b574ac4c59097ecde8d9cfd681b47ae84'
  aarch64 = '47ea5a0ed661bea53962d8fb8ee9a1769092d979a6f7c73d3081499b3305b2ab'
}[$flavor]

$name = "ccache-$version-windows-$flavor"
$zip = Join-Path $env:RUNNER_TEMP "$name.zip"
$url = "https://github.com/ccache/ccache/releases/download/v$version/$name.zip"
# GitHub release downloads fail now and then (HTTP 504). Retry, and count a wrong checksum as a
# failed attempt too: a truncated download looks like a mismatch.
$attempts = 6
for ($i = 1; $i -le $attempts; $i++) {
  try {
    Invoke-WebRequest $url -OutFile $zip -TimeoutSec 120
    if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $sha256) { throw "$name.zip checksum mismatch" }
    break
  } catch {
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    if ($i -eq $attempts) { throw "could not download $url after $attempts attempts: $_" }
    $wait = 5 * $i * $i
    Write-Warning "attempt $i of $attempts failed ($($_.Exception.Message)); retrying in ${wait}s"
    Start-Sleep -Seconds $wait
  }
}
Expand-Archive $zip -DestinationPath $env:RUNNER_TEMP

(Join-Path $env:RUNNER_TEMP $name) >> $env:GITHUB_PATH
"CMAKE_CXX_COMPILER_LAUNCHER=ccache" >> $env:GITHUB_ENV
"CCACHE_DIR=$env:GITHUB_WORKSPACE\.ccache" >> $env:GITHUB_ENV
"CCACHE_MAXSIZE=600M" >> $env:GITHUB_ENV
"CCACHE_COMPILERCHECK=content" >> $env:GITHUB_ENV
