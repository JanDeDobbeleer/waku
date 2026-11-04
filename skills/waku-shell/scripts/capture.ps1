<#
.SYNOPSIS
    DPI-aware screenshot of the desktop, with an optional crop.
.DESCRIPTION
    Makes the process DPI aware before it reads any size, so the capture has the real pixel size of
    the screen (a DPI-unaware process gets a scaled-down image). Prints the size it captured.
.PARAMETER Out
    PNG file to write. The folder is created.
.PARAMETER Crop
    x,y,width,height in physical pixels of the captured area. Default: the whole area.
.PARAMETER Screen
    Index of one screen (0 is the first in [System.Windows.Forms.Screen]::AllScreens). Default: all
    screens (the virtual desktop).
.EXAMPLE
    pwsh -NoProfile -File capture.ps1 -Out $env:TEMP\shot.png -Crop 0,0,3840,120
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Out,
    [string]$Crop,     # "x,y,width,height"; a string because `pwsh -File` passes -Crop 0,0,9,9 as one
    [Nullable[int]]$Screen
)
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -Namespace Waku -Name Dpi -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetProcessDpiAwarenessContext(System.IntPtr value);
'@
# DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 is -4.
[void][Waku.Dpi]::SetProcessDpiAwarenessContext([IntPtr](-4))

if ($null -ne $Screen) {
    $screens = [System.Windows.Forms.Screen]::AllScreens
    if ($Screen -lt 0 -or $Screen -ge $screens.Count) { throw "Screen $Screen does not exist (0..$($screens.Count - 1))" }
    $area = $screens[$Screen].Bounds
} else {
    $area = [System.Windows.Forms.SystemInformation]::VirtualScreen
}

$bmp = [System.Drawing.Bitmap]::new($area.Width, $area.Height)
try {
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try { $g.CopyFromScreen($area.X, $area.Y, 0, 0, $bmp.Size) } finally { $g.Dispose() }

    $result = $bmp
    if ($Crop) {
        $parts = @($Crop -split '[,\s]+' | Where-Object { $_ -ne '' } | ForEach-Object { [int]$_ })
        if ($parts.Count -ne 4) { throw '-Crop takes x,y,width,height' }
        $x, $y, $w, $h = $parts
        if ($x -lt 0 -or $y -lt 0 -or $w -le 0 -or $h -le 0 -or $x + $w -gt $bmp.Width -or $y + $h -gt $bmp.Height) {
            throw "Crop $Crop is outside the captured $($bmp.Width)x$($bmp.Height) area"
        }
        $result = $bmp.Clone([System.Drawing.Rectangle]::new($x, $y, $w, $h), $bmp.PixelFormat)
    }

    $dir = Split-Path -Parent ([System.IO.Path]::GetFullPath($Out))
    [void](New-Item -ItemType Directory -Force -Path $dir)
    $result.Save([System.IO.Path]::GetFullPath($Out), [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Host "captured $($area.Width)x$($area.Height) at $($area.X),$($area.Y); wrote $($result.Width)x$($result.Height) to $Out"
    if ($result -ne $bmp) { $result.Dispose() }
} finally {
    $bmp.Dispose()
}
