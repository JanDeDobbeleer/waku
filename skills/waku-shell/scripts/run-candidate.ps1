<#
.SYNOPSIS
    Start a candidate shell in isolation, report its status, then stop it.
.DESCRIPTION
    Runs waku with --config-home <ConfigHome> --no-tray and a log file in a temp folder. It never
    passes --register-app-bar, --hide-taskbar or --manage-komorebi, so the desktop layout, the
    Windows taskbar and komorebi are left alone. By default it uses the offscreen platform (level 1:
    the shell loads) when the engine folder has that plugin, else the real platform. -Visible uses
    the real platform so you can capture pixels (level 2); a shell window appears on the desktop
    until the script stops it.
    Prints the `wakuc status` line and every warning or critical log record. Exit 0 only when the
    status says error=none and no warning or critical record appeared.
.PARAMETER ConfigHome
    The candidate config home. Must not be the user's real config (%USERPROFILE%\.config\waku).
.PARAMETER Waku
    Folder with waku.exe and wakuc.exe. Default: $env:WAKU_DIR, else dist\waku of this repository,
    else the folder of waku.exe on PATH.
.PARAMETER Visible
    Use the real platform instead of offscreen.
.PARAMETER Seconds
    Seconds to wait for the shell to start (and, with -Hold, to stay up). Default 6.
.PARAMETER Hold
    With -Visible, keep the shell running this many seconds after the status check so you can
    capture it, then stop it. Default 0.
.EXAMPLE
    pwsh -NoProfile -File run-candidate.ps1 -ConfigHome D:\shells\mine
.EXAMPLE
    pwsh -NoProfile -File run-candidate.ps1 -ConfigHome D:\shells\mine -Visible -Hold 20
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ConfigHome,
    [string]$Waku,
    [switch]$Visible,
    [int]$Seconds = 6,
    [int]$Hold = 0
)
$ErrorActionPreference = 'Stop'

$home1 = [System.IO.Path]::GetFullPath($ConfigHome).TrimEnd('\')
$real = [System.IO.Path]::GetFullPath((Join-Path $env:USERPROFILE '.config\waku')).TrimEnd('\')
if ($home1 -ieq $real) { throw 'Refusing to run against the real config home. Copy it to a throwaway folder.' }
if (-not (Test-Path -LiteralPath (Join-Path $home1 'shell.qml'))) { throw "No shell.qml in $home1" }

# Find the engine.
$dir = $Waku
if (-not $dir) { $dir = $env:WAKU_DIR }
if (-not $dir) {
    $repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..') -ErrorAction SilentlyContinue
    if ($repo -and (Test-Path (Join-Path $repo 'dist\waku\waku.exe'))) { $dir = Join-Path $repo 'dist\waku' }
}
if (-not $dir) {
    $cmd = Get-Command waku.exe -ErrorAction SilentlyContinue
    if ($cmd) { $dir = Split-Path -Parent $cmd.Source }
}
if (-not $dir -or -not (Test-Path (Join-Path $dir 'waku.exe')) -or -not (Test-Path (Join-Path $dir 'wakuc.exe'))) {
    throw 'waku.exe and wakuc.exe not found. Pass -Waku <folder> or set WAKU_DIR.'
}
$wakuExe = Join-Path $dir 'waku.exe'
$wakucExe = Join-Path $dir 'wakuc.exe'

$work = Join-Path ([System.IO.Path]::GetTempPath()) ('waku-candidate-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
[void](New-Item -ItemType Directory -Path $work)
$log = Join-Path $work 'waku.log'
$stdout = Join-Path $work 'stdout.txt'
$stderr = Join-Path $work 'stderr.txt'

# A deployed engine (MSIX, dist\waku) has only the Windows platform plugin. Offscreen needs
# platforms\qoffscreen*.dll next to waku.exe; without it, run on the real platform (a shell window
# appears on the desktop for the length of the run) and say so.
$offscreen = -not $Visible
if ($offscreen -and -not (Get-ChildItem -Path (Join-Path $dir 'platforms') -Filter 'qoffscreen*.dll' -ErrorAction SilentlyContinue)) {
    Write-Host 'no offscreen platform plugin next to waku.exe: running on the real platform (windows appear on the desktop)'
    $offscreen = $false
}
$oldQpa = $env:QT_QPA_PLATFORM
if ($offscreen) { $env:QT_QPA_PLATFORM = 'offscreen' }
$proc = $null
try {
    $proc = Start-Process -FilePath $wakuExe -PassThru -WindowStyle Hidden `
        -ArgumentList @('--config-home', $home1, '--no-tray', '--log-file', $log) `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $env:QT_QPA_PLATFORM = $oldQpa

    # Wait for the control pipe to answer.
    $status = ''
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if ($proc.HasExited) { break }
        $status = (& $wakucExe --config-home $home1 status 2>&1 | Out-String).Trim()
        if ($LASTEXITCODE -eq 0 -and $status -match 'windows=\d+') { break }
    }

    if ($proc.HasExited) { Write-Host "waku exited early with code $($proc.ExitCode)" }
    Write-Host "status: $status"

    if ($Visible -and $Hold -gt 0 -and -not $proc.HasExited) {
        Write-Host "holding $Hold s for a capture (scripts\capture.ps1)"
        Start-Sleep -Seconds $Hold
    }
} finally {
    $env:QT_QPA_PLATFORM = $oldQpa
    if ($proc -and -not $proc.HasExited) {
        & $wakucExe --config-home $home1 quit 2>&1 | Out-Null
        if (-not $proc.WaitForExit(5000)) { Stop-Process -Id $proc.Id -Force }
    }
}

# Read the log: warning and critical records.
$bad = @()
if (Test-Path $log) {
    foreach ($line in Get-Content -LiteralPath $log) {
        try { $r = $line | ConvertFrom-Json } catch { continue }
        # The offscreen platform has no font folder; that warning is not about the shell.
        if ($offscreen -and $r.msg -match 'QFontDatabase: Cannot find font directory') { continue }
        if ($r.k -eq 'log' -and $r.lvl -in 'warning', 'critical', 'fatal') { $bad += $r }
    }
}
if (Test-Path $stderr) {
    $err = Get-Content -LiteralPath $stderr -Raw
    if ($err) { Write-Host "stderr:`n$err" }
}
foreach ($r in $bad) { Write-Host ('{0,-8} {1}' -f $r.lvl, $r.msg) }
Write-Host "log: $log"

$ok = ($status -match 'error=none') -and ($bad.Count -eq 0)
if ($ok) {
    Write-Host "level 1 reached: loads, error=none, no warnings."
    if ($offscreen) { Write-Host 'offscreen: no pixels were drawn and no panel was opened.' }
    exit 0
}
Write-Host 'level 1 NOT reached. Fix the findings above.'
exit 1
