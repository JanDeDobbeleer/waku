# Shared helpers for the A/B harness. Dot-source from Switch-Bar.ps1 and Measure-Bar.ps1.
# Windows PowerShell 5.1 compatible. Processes are identified by full executable path or PID, never by name.

$script:Builds = @{
    python = @{
        Exe      = 'C:\Program Files\YASB\yasb.exe'
        Cli      = 'C:\Program Files\YASB\yasbc.exe'
        Log      = (Join-Path $env:USERPROFILE '.config\yasb\yasb.log')
        HookDlls = @('YASBTrayHook.dll', 'YASBTrayHook_arm64.dll')
    }
    native = @{
        Exe      = $null
        Cli      = $null
        Log      = (Join-Path $env:USERPROFILE '.config\yasb\yasb-native.log')
        HookDlls = @('YASBNativeTrayHook.dll', 'YASBNativeTrayHook_arm64.dll')
    }
}

function Write-Step([string]$Message) {
    Write-Host ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss.fff'), $Message)
}

function Get-BuildInfo([string]$Build, [string]$NativeExe, [string]$NativeCli) {
    $info = $script:Builds[$Build].Clone()
    if ($Build -eq 'native') {
        if ($NativeExe) { $info.Exe = $NativeExe }
        if (-not $info.Exe) { $info.Exe = 'yasb-native.exe (pass -NativeExe)' }
        if ($NativeCli) {
            $info.Cli = $NativeCli
        } elseif ($NativeExe) {
            $info.Cli = Join-Path (Split-Path -Parent $NativeExe) 'yasbc-native.exe'
        }
    }
    return $info
}

function Get-ProcessesByPath([string]$ExePath) {
    Get-CimInstance Win32_Process |
        Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $ExePath) }
}

function Get-ExplorerProcessIds {
    $explorer = Join-Path $env:WINDIR 'explorer.exe'
    @(Get-ProcessesByPath $explorer | ForEach-Object { [int]$_.ProcessId })
}

function Get-LoadedHookDlls([string[]]$DllNames) {
    $found = @()
    foreach ($id in (Get-ExplorerProcessIds)) {
        $process = Get-Process -Id $id -ErrorAction SilentlyContinue
        if (-not $process) { continue }
        try {
            foreach ($module in $process.Modules) {
                if ($DllNames -contains $module.ModuleName) { $found += ("{0} (explorer pid {1})" -f $module.ModuleName, $id) }
            }
        } catch {
            # Modules of a protected or exiting process cannot be listed; treat as none loaded.
        }
    }
    return $found
}

function Wait-HookUnload([string[]]$DllNames, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $loaded = @(Get-LoadedHookDlls $DllNames)
        if ($loaded.Count -eq 0) { return $true }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    Write-Warning ("Hook DLL(s) still loaded: " + ($loaded -join ', '))
    return $false
}

function Get-LogOffset([string]$LogPath) {
    if (Test-Path -LiteralPath $LogPath) { return (Get-Item -LiteralPath $LogPath).Length }
    return 0
}

function Read-LogFrom([string]$LogPath, [long]$Offset) {
    if (-not (Test-Path -LiteralPath $LogPath)) { return '' }
    $stream = New-Object System.IO.FileStream($LogPath, 'Open', 'Read', 'ReadWrite, Delete')
    try {
        if ($Offset -gt $stream.Length) { $Offset = 0 }
        [void]$stream.Seek($Offset, 'Begin')
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        return $reader.ReadToEnd()
    } finally {
        $stream.Dispose()
    }
}

function Wait-LogLine([string]$LogPath, [long]$Offset, [string]$Pattern, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $match = [regex]::Match((Read-LogFrom $LogPath $Offset), "(?m)^.*$Pattern.*$")
        if ($match.Success) { return $match.Value.Trim() }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return $null
}

function Stop-Build($Info, [int]$TimeoutSeconds, [bool]$Dry) {
    $running = @(Get-ProcessesByPath $Info.Exe)
    if ($running.Count -eq 0) { Write-Step "Not running: $($Info.Exe)"; return }
    $ids = @($running | ForEach-Object { [int]$_.ProcessId })
    if ($Dry) {
        Write-Step "WHATIF: run '$($Info.Cli) stop', wait up to ${TimeoutSeconds}s for PID(s) $($ids -join ', '); force kill by PID only on timeout"
        return
    }
    Write-Step "Stopping via CLI: $($Info.Cli) stop (PID $($ids -join ', '))"
    if ($Info.Cli -and (Test-Path -LiteralPath $Info.Cli)) {
        Start-Process -FilePath $Info.Cli -ArgumentList 'stop' -WindowStyle Hidden | Out-Null
    } else {
        Write-Warning "CLI not found ($($Info.Cli)); waiting for the process to exit before the timeout fallback"
    }
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $left = @($ids | Where-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
        if ($left.Count -eq 0) { return }
        Start-Sleep -Milliseconds 300
    }
    foreach ($id in $ids) {
        if (Get-Process -Id $id -ErrorAction SilentlyContinue) {
            Write-Warning "Timeout: force killing PID $id"
            Stop-Process -Id $id -Force
        }
    }
}

function Start-Build($Info, [bool]$Dry, [string]$ConfigHome = '', [string]$Build = '', [bool]$RegisterAppBar = $false) {
    if ($Dry) {
        if ($ConfigHome) {
            Write-Step "WHATIF: start $Build directly with isolated config home $ConfigHome"
        } else {
            Write-Step "WHATIF: start through explorer: explorer.exe `"$($Info.Exe)`""
        }
        return
    }
    if (-not (Test-Path -LiteralPath $Info.Exe)) { throw "Executable not found: $($Info.Exe)" }
    if ($ConfigHome) {
        if ($Build -eq 'python') {
            $previousConfigHome = $env:YASB_CONFIG_HOME
            $env:YASB_CONFIG_HOME = $ConfigHome
            try {
                Start-Process -FilePath $Info.Exe | Out-Null
            } finally {
                if ($null -eq $previousConfigHome) {
                    Remove-Item Env:YASB_CONFIG_HOME -ErrorAction SilentlyContinue
                } else {
                    $env:YASB_CONFIG_HOME = $previousConfigHome
                }
            }
        } elseif ($Build -eq 'native') {
            $arguments = @('--config-home', ('"{0}"' -f $ConfigHome))
            if ($RegisterAppBar) { $arguments += '--register-app-bar' }
            Start-Process -FilePath $Info.Exe -ArgumentList $arguments | Out-Null
        } else {
            throw "Unknown build target: $Build"
        }
        return
    }
    Start-Process -FilePath (Join-Path $env:WINDIR 'explorer.exe') -ArgumentList ('"{0}"' -f $Info.Exe) | Out-Null
}

function Get-RunningPid($Info) {
    $match = @(Get-ProcessesByPath $Info.Exe) | Select-Object -First 1
    if ($match) { return [int]$match.ProcessId }
    return $null
}

function Enable-DpiAwareness {
    if (-not ('AbNative' -as [type])) {
        Add-Type -TypeDefinition 'using System.Runtime.InteropServices; public static class AbNative { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }'
    }
    [void][AbNative]::SetProcessDPIAware()
}

function Save-BarCapture([string]$Path, [int[]]$Region) {
    Add-Type -AssemblyName System.Drawing, System.Windows.Forms
    Enable-DpiAwareness
    $bmp = New-Object System.Drawing.Bitmap $Region[2], $Region[3]
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $graphics.CopyFromScreen($Region[0], $Region[1], 0, 0, $bmp.Size)
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bmp.Dispose()
    }
}

function Resolve-BarRegion([string]$Region, [int]$BarHeightPx, [string]$Edge) {
    if ($Region) {
        $parts = @($Region -split '[,\s]+' | Where-Object { $_ } | ForEach-Object { [int]$_ })
        if ($parts.Count -ne 4) { throw "-Region must be 'x,y,width,height'" }
        return $parts
    }
    Add-Type -AssemblyName System.Windows.Forms
    Enable-DpiAwareness
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $y = if ($Edge -eq 'bottom') { $bounds.Bottom - $BarHeightPx } else { $bounds.Top }
    return @($bounds.Left, $y, $bounds.Width, $BarHeightPx)
}
