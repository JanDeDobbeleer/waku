# Installs vcpkg.exe into the vcpkg checkout without bootstrap-vcpkg.bat. The bootstrap script
# downloads the tool once, with no retry, so a single 504 from GitHub fails the job. This script
# does what it does (download the release named in scripts/vcpkg-tool-metadata.txt, disable
# metrics) and adds retries and a signature check.
#
#   -Mode Tag      write the tool release tag to GITHUB_OUTPUT (the cache key)
#   -Mode Install  use .vcpkg-tool\vcpkg.exe when the cache restored it, else download it
param(
  [Parameter(Mandatory)][ValidateSet('Tag', 'Install')][string]$Mode,
  [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Arch
)

$ErrorActionPreference = 'Stop'
$root = Join-Path $env:GITHUB_WORKSPACE 'vcpkg'
$cacheDir = Join-Path $env:GITHUB_WORKSPACE '.vcpkg-tool'

$tag = (Get-Content (Join-Path $root 'scripts\vcpkg-tool-metadata.txt') |
    Where-Object { $_ -match '^VCPKG_TOOL_RELEASE_TAG=(.+)$' } |
    ForEach-Object { $Matches[1].Trim() } | Select-Object -First 1)
if (-not $tag) { throw 'VCPKG_TOOL_RELEASE_TAG not found in vcpkg-tool-metadata.txt' }

if ($Mode -eq 'Tag') {
  "tag=$tag" >> $env:GITHUB_OUTPUT
  return
}

$asset = if ($Arch -eq 'arm64') { 'vcpkg-arm64.exe' } else { 'vcpkg.exe' }
$cached = Join-Path $cacheDir 'vcpkg.exe'

# The release is signed by Microsoft. A cached or downloaded file that is not is discarded.
function Test-Signed([string]$path) {
  $sig = Get-AuthenticodeSignature $path
  $sig.Status -eq 'Valid' -and $sig.SignerCertificate.Subject -match 'O=Microsoft Corporation'
}

if ((Test-Path $cached) -and -not (Test-Signed $cached)) {
  Write-Warning 'cached vcpkg.exe failed the signature check, downloading it again'
  Remove-Item -LiteralPath $cached -Force
}

if (-not (Test-Path $cached)) {
  New-Item -ItemType Directory -Force $cacheDir | Out-Null
  $url = "https://github.com/microsoft/vcpkg-tool/releases/download/$tag/$asset"
  $attempts = 6
  for ($i = 1; $i -le $attempts; $i++) {
    try {
      Invoke-WebRequest $url -OutFile $cached -TimeoutSec 120
      if (-not (Test-Signed $cached)) { throw "$asset is not validly signed by Microsoft Corporation" }
      break
    } catch {
      Remove-Item -LiteralPath $cached -Force -ErrorAction SilentlyContinue
      if ($i -eq $attempts) { throw "could not download $url after $attempts attempts: $_" }
      $wait = 5 * $i * $i
      Write-Warning "attempt $i of $attempts failed ($($_.Exception.Message)); retrying in ${wait}s"
      Start-Sleep -Seconds $wait
    }
  }
}

Copy-Item -LiteralPath $cached -Destination (Join-Path $root 'vcpkg.exe') -Force
& (Join-Path $root 'vcpkg.exe') version --disable-metrics
if ($LASTEXITCODE -ne 0) { throw 'vcpkg.exe does not run' }
Set-Content -Value '' -Path (Join-Path $root 'vcpkg.disable-metrics') -Force
