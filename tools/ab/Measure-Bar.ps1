<#
.SYNOPSIS
    Measures the running YASB build: private bytes, idle CPU, context switches, handles, threads.
.DESCRIPTION
    Read-only. Finds the process by full executable path (or -ProcessId), samples it every
    -SampleSeconds for -IdleMinutes, and writes a JSON summary to tools\ab\out\. Leave the machine
    idle while it runs. CPU is percent of one logical core. Context switches come from the
    Win32_PerfRawData_PerfProc_Process counter (cumulative, so the rate is a delta over the window).
    Pass -SwitchRecord (the JSON written by Switch-Bar.ps1) to include time-to-bar-visible.
    Use -WhatIf (or -DryRun) to print the plan only.
.EXAMPLE
    .\Measure-Bar.ps1 -Target python -IdleMinutes 10 -Label baseline-run1
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)][ValidateSet('python', 'native')][string]$Target,
    [string]$NativeExe,
    [int]$ProcessId,
    [double]$IdleMinutes = 10,
    [int]$SampleSeconds = 5,
    [string]$SwitchRecord,
    [string]$OutDir,
    [string]$Label = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'AbCommon.ps1')
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot 'out' }

$dry = [bool]($WhatIfPreference -or $DryRun)
$WhatIfPreference = $false # dry runs are handled explicitly; keeps module auto-loading quiet
$info = Get-BuildInfo $Target $NativeExe $null
$samples = [math]::Max(2, [int][math]::Ceiling($IdleMinutes * 60 / $SampleSeconds))

if ($ProcessId) { $targetPid = $ProcessId } else { $targetPid = Get-RunningPid $info }
if ($dry) {
    $pidText = if ($targetPid) { $targetPid } else { 'none running' }
    Write-Step ("DRY RUN: would sample PID {0} ({1}) {2} times every {3}s (~{4} min)" -f $pidText, $info.Exe, $samples, $SampleSeconds, $IdleMinutes)
    Write-Step ("DRY RUN: would write " + (Join-Path $OutDir "$Target-measure-<timestamp>.json"))
    return
}
if (-not $targetPid) { throw "No running process with path $($info.Exe)" }

function Get-ContextSwitchTotal([int]$ProcessIdToRead) {
    $counter = Get-CimInstance Win32_PerfRawData_PerfProc_Process -Filter "IDProcess=$ProcessIdToRead"
    if ($counter -and $null -ne $counter.ContextSwitchesPersec -and "$($counter.ContextSwitchesPersec)" -ne '') {
        return [double]$counter.ContextSwitchesPersec
    }
    return $null
}

$rows = New-Object System.Collections.ArrayList
$process = Get-Process -Id $targetPid
$startCpu = $process.TotalProcessorTime.TotalSeconds
$startSwitches = Get-ContextSwitchTotal $targetPid
if ($null -eq $startSwitches) { Write-Warning 'Context-switch counter is unavailable; recording null.' }
$clock = [System.Diagnostics.Stopwatch]::StartNew()
Write-Step "Sampling PID $targetPid for $IdleMinutes min"

for ($i = 0; $i -lt $samples; $i++) {
    Start-Sleep -Seconds $SampleSeconds
    $process = Get-Process -Id $targetPid -ErrorAction Stop
    [void]$rows.Add([pscustomobject]@{
            t            = [math]::Round($clock.Elapsed.TotalSeconds, 1)
            privateBytes = $process.PrivateMemorySize64
            workingSet   = $process.WorkingSet64
            handles      = $process.HandleCount
            threads      = $process.Threads.Count
            cpuSeconds   = $process.TotalProcessorTime.TotalSeconds
        })
}

$elapsed = $clock.Elapsed.TotalSeconds
$last = $rows[$rows.Count - 1]
$privateSorted = @($rows | ForEach-Object { $_.privateBytes } | Sort-Object)
$endSwitches = Get-ContextSwitchTotal $targetPid
$contextSwitchesPerSec = if ($null -ne $startSwitches -and $null -ne $endSwitches) {
    [math]::Round(($endSwitches - $startSwitches) / $elapsed, 1)
} else {
    $null
}
$summary = [ordered]@{
    target                  = $Target
    exe                     = $info.Exe
    pid                     = $targetPid
    logicalCores            = [Environment]::ProcessorCount
    windowSeconds           = [math]::Round($elapsed, 1)
    samples                 = $rows.Count
    privateBytesMedian      = $privateSorted[[int][math]::Floor($privateSorted.Count / 2)]
    privateBytesMax         = $privateSorted[$privateSorted.Count - 1]
    privateBytesEnd         = $last.privateBytes
    idleCpuPercentOfCore    = [math]::Round(100.0 * ($last.cpuSeconds - $startCpu) / $elapsed, 3)
    contextSwitchesPerSec   = $contextSwitchesPerSec
    handlesEnd              = $last.handles
    threadsEnd              = $last.threads
    timeToBarVisibleSeconds = $null
    measuredAt              = (Get-Date).ToString('o')
    machine                 = $env:COMPUTERNAME
}
if ($SwitchRecord) {
    $summary.timeToBarVisibleSeconds = (Get-Content -LiteralPath $SwitchRecord -Raw | ConvertFrom-Json).readySeconds
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$name = "$Target-measure-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + $(if ($Label) { "-$Label" } else { '' })
$result = [ordered]@{ summary = $summary; samples = $rows }
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir "$name.json") -Encoding UTF8
$summary | ConvertTo-Json
