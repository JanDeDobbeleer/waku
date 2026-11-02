<#
.SYNOPSIS
    Packages a deployed waku folder as an MSIX, optionally signed with Azure Trusted Signing.

.PARAMETER Architecture
    x64 or arm64. Must match the build in -Source (each holds its own tray hook DLL).

.PARAMETER Version
    Three-part version without a leading v, for example 0.1.0. The package version is Version.0.

.PARAMETER Source
    The deployed folder: the output of `cmake --build --preset <preset> --target deploy`
    (dist/waku). Default: ../../dist/waku.

.PARAMETER Sign
    Sign the package with Azure Trusted Signing (signing.json). Needs AZURE_CLIENT_ID,
    AZURE_CLIENT_SECRET and AZURE_TENANT_ID in the environment.

.PARAMETER Repository
    GitHub owner/name that hosts the releases, used in the App Installer file.
    Default: $env:GITHUB_REPOSITORY, else JanDeDobbeleer/waku.

.EXAMPLE
    ./build.ps1 -Architecture arm64 -Version 0.1.0

.OUTPUTS
    out/waku-<arch>.msix and out/waku-<arch>.appinstaller
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Architecture,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$Source = (Join-Path $PSScriptRoot '../../dist/waku'),
    [switch]$Sign,
    [string]$Repository = $(if ($env:GITHUB_REPOSITORY) { $env:GITHUB_REPOSITORY } else { 'JanDeDobbeleer/waku' }),
    [string]$SigningToolsVersion = '1.0.92'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

function Find-SdkTool([string]$Name) {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    $tool = Get-ChildItem -Path $kits -Directory -Filter '10.*' |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "x64/$Name" } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
    if (-not $tool) { throw "$Name not found under $kits (install the Windows SDK)" }
    return $tool
}

$Source = (Resolve-Path $Source).Path
foreach ($file in 'waku.exe', 'wakuc.exe') {
    if (-not (Test-Path (Join-Path $Source $file))) { throw "$file not found in $Source" }
}
$hook = if ($Architecture -eq 'arm64') { 'WakuTrayHook_arm64.dll' } else { 'WakuTrayHook.dll' }
if (-not (Test-Path (Join-Path $Source $hook))) { throw "$hook not found in $Source (wrong -Architecture?)" }

$out = Join-Path $PSScriptRoot 'out'
$stage = Join-Path $PSScriptRoot "stage-$Architecture"
New-Item -ItemType Directory -Force -Path $out | Out-Null
if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Write-Host "Staging $Source"
Copy-Item -Path (Join-Path $Source '*') -Destination $stage -Recurse -Force
Copy-Item -Path (Join-Path $PSScriptRoot 'icons') -Destination (Join-Path $stage 'icons') -Recurse -Force

$base = "https://github.com/$Repository/releases"
$msixName = "waku-$Architecture.msix"
$appInstallerName = "waku-$Architecture.appinstaller"
$pkgVersion = "$Version.0"

# Manifest: set the version and architecture.
[xml]$manifest = Get-Content (Join-Path $PSScriptRoot 'appxmanifest.xml')
$identity = $manifest.Package.Identity
$identity.Version = $pkgVersion
$identity.ProcessorArchitecture = $Architecture

# The App Installer file names the stable "latest" URL that Windows polls for updates and the
# immutable per-version package URL. The copy inside the package is always install.appinstaller
# (the name the manifest refers to); the release also carries it as waku-<arch>.appinstaller.
$appInstaller = @"
<?xml version="1.0" encoding="utf-8"?>
<AppInstaller xmlns="http://schemas.microsoft.com/appx/appinstaller/2018" Version="$pkgVersion" Uri="$base/latest/download/$appInstallerName">
  <MainPackage Name="$($identity.Name)" Version="$pkgVersion" Publisher="$($identity.Publisher)" ProcessorArchitecture="$Architecture" Uri="$base/download/v$Version/$msixName" />
  <UpdateSettings>
    <AutomaticBackgroundTask />
  </UpdateSettings>
</AppInstaller>
"@
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path $stage 'install.appinstaller'), $appInstaller, $utf8)
[System.IO.File]::WriteAllText((Join-Path $out $appInstallerName), $appInstaller, $utf8)

$writer = New-Object System.Xml.XmlTextWriter((Join-Path $stage 'AppxManifest.xml'), $utf8)
$writer.Formatting = 'Indented'
$manifest.Save($writer)
$writer.Close()

$msix = Join-Path $out $msixName
& (Find-SdkTool 'makeappx.exe') pack /d $stage /p $msix /o /nv
if (-not (Test-Path $msix)) { throw "MSIX was not created: $msix" }
Remove-Item -LiteralPath $stage -Recurse -Force

if ($Sign) {
    $signtool = Find-SdkTool 'signtool.exe'
    $work = Join-Path $PSScriptRoot 'signing-tools'
    nuget.exe install Microsoft.Trusted.Signing.Client -Version $SigningToolsVersion -OutputDirectory $work -ExcludeVersion | Out-Null
    $dlib = Join-Path $work 'Microsoft.Trusted.Signing.Client/bin/x64/Azure.CodeSigning.Dlib.dll'
    if (-not (Test-Path $dlib)) { throw "Azure.CodeSigning.Dlib.dll not found at $dlib" }

    & $signtool sign /v /d 'waku' /fd SHA256 /tr 'http://timestamp.acs.microsoft.com' /td SHA256 `
        /dlib $dlib /dmdf (Join-Path $PSScriptRoot 'signing.json') $msix
}

$state = if ($Sign) { 'signed' } else { 'unsigned' }
Write-Host "Built $msix ($state)"
