<#
.SYNOPSIS
    Swaps the running YASB build (python <-> native) and captures the bar.
.DESCRIPTION
    1. Stops the other build via its CLI `stop` (force kill by PID only after -StopTimeoutSeconds).
    2. Waits until none of the tray hook DLLs is loaded in explorer.exe.
    3. Starts the target build through explorer.exe so it inherits a normal environment.
    4. Waits for the readiness line in the target's log (new lines only).
    5. Captures the bar region to tools\ab\out\ and writes a small JSON record next to it.
    Processes are matched by full executable path or PID, never by name.
    Use -WhatIf (or -DryRun) to print the plan without touching any process.
.PARAMETER Target
    Build to run after the swap: python or native.
.PARAMETER NativeExe
    Full path of yasb-native.exe. Required for -Target native (and to stop it when swapping away).
.PARAMETER NativeCli
    Full path of the native CLI client. Defaults to yasbc-native.exe next to -NativeExe.
.PARAMETER ConfigHome
    Isolated config directory. Starts directly instead of through Explorer; use only for tests without a tray hook.
.PARAMETER RegisterAppBar
    Register the native app bar when starting with -ConfigHome.
.PARAMETER Settle
    Seconds to wait after the readiness line before capturing, so widgets finish their first update.
.PARAMETER ReadyPattern
    Regex matched against new log lines. Default matches the "Bar ... Created on Screen" line.
.PARAMETER Region
    Capture rectangle "x,y,width,height" in physical pixels. Default: primary monitor edge strip.
.PARAMETER Label
    Free text added to the capture file name.
.EXAMPLE
    .\Switch-Bar.ps1 -Target native -NativeExe D:\build\yasb-native.exe -WhatIf
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][ValidateSet('python', 'native')][string]$Target,
    [string]$NativeExe,
    [string]$NativeCli,
    [string]$ConfigHome,
    [switch]$RegisterAppBar,
    [int]$StopTimeoutSeconds = 20,
    [int]$HookUnloadTimeoutSeconds = 30,
    [int]$StartTimeoutSeconds = 60,
    [int]$Settle = 5,
    [string]$ReadyPattern = 'Created on Screen',
    [string]$Region,
    [int]$BarHeightPx = 72,
    [ValidateSet('top', 'bottom')][string]$Edge = 'top',
    [string]$OutDir,
    [string]$Label = '',
    [switch]$SkipCapture,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'AbCommon.ps1')
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot 'out' }

$dry = [bool]($WhatIfPreference -or $DryRun)
$WhatIfPreference = $false # dry runs are handled explicitly; keeps module auto-loading quiet
$other = if ($Target -eq 'python') { 'native' } else { 'python' }
$targetInfo = Get-BuildInfo $Target $NativeExe $NativeCli
$otherInfo = Get-BuildInfo $other $NativeExe $NativeCli
if ($ConfigHome) {
    if (-not (Test-Path -LiteralPath $ConfigHome -PathType Container)) { throw "Config home not found: $ConfigHome" }
    $logName = if ($Target -eq 'python') { 'yasb.log' } else { 'yasb-native.log' }
    $targetInfo.Log = Join-Path $ConfigHome $logName
}
$allHookDlls = @($script:Builds.python.HookDlls) + @($script:Builds.native.HookDlls)

if ($dry) { Write-Step "DRY RUN: nothing will be stopped, started or captured" }
Write-Step "Swap $other -> $Target"

Stop-Build $otherInfo $StopTimeoutSeconds $dry
Stop-Build $targetInfo $StopTimeoutSeconds $dry

if ($dry) {
    $loaded = @(Get-LoadedHookDlls $allHookDlls)
    Write-Step ("WHATIF: wait up to ${HookUnloadTimeoutSeconds}s for hook DLLs to unload from explorer.exe; currently loaded: " + $(if ($loaded) { $loaded -join ', ' } else { 'none' }))
} elseif (-not (Wait-HookUnload $allHookDlls $HookUnloadTimeoutSeconds)) {
    throw 'Tray hook DLL still loaded in explorer.exe; not starting the next build'
}

$offset = Get-LogOffset $targetInfo.Log
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
Start-Build $targetInfo $dry $ConfigHome $Target ([bool]$RegisterAppBar)

$readyLine = $null
if (-not $dry) {
    $readyLine = Wait-LogLine $targetInfo.Log $offset $ReadyPattern $StartTimeoutSeconds
    $stopwatch.Stop()
    if (-not $readyLine) { throw "No log line matching '$ReadyPattern' in $($targetInfo.Log) within ${StartTimeoutSeconds}s" }
    Write-Step ("Ready after {0:N1}s: {1}" -f $stopwatch.Elapsed.TotalSeconds, $readyLine)
} else {
    Write-Step "WHATIF: wait up to ${StartTimeoutSeconds}s for a new line matching '$ReadyPattern' in $($targetInfo.Log)"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$name = "$Target-$stamp" + $(if ($Label) { "-$Label" } else { '' })
$capturePath = Join-Path $OutDir "$name.png"
if ($SkipCapture) {
    Write-Step 'Capture skipped'
} elseif ($dry) {
    $regionText = if ($Region) { $Region } else { "primary monitor $Edge strip, ${BarHeightPx}px high" }
    Write-Step "WHATIF: wait ${Settle}s, capture [$regionText] to $capturePath"
} else {
    Start-Sleep -Seconds $Settle
    $rect = Resolve-BarRegion $Region $BarHeightPx $Edge
    Save-BarCapture $capturePath $rect
    $record = [ordered]@{
        target        = $Target
        exe           = $targetInfo.Exe
        pid           = Get-RunningPid $targetInfo
        readySeconds  = [math]::Round($stopwatch.Elapsed.TotalSeconds, 2)
        readyLine     = $readyLine
        region        = $rect
        capture       = $capturePath
        capturedAt    = (Get-Date).ToString('o')
    }
    $record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir "$name.json") -Encoding UTF8
    Write-Step "Captured $capturePath"
}
