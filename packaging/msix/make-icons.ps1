<#
.SYNOPSIS
    Draws the placeholder package icons (a window frame) into ./icons.

.DESCRIPTION
    Run once; the PNGs are committed. Replace them with real artwork and keep the file names
    (appxmanifest.xml refers to them).
#>
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$out = Join-Path $PSScriptRoot 'icons'
New-Item -ItemType Directory -Force -Path $out | Out-Null

function New-Icon([int]$Size, [string]$Name) {
    $bmp = New-Object System.Drawing.Bitmap $Size, $Size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.ColorTranslator]::FromHtml('#1e2030'))
    $inset = [float]($Size * 0.18)
    $pen = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml('#c6d0f5')), ([float]($Size * 0.09))
    $g.DrawRectangle($pen, $inset, $inset, $Size - 2 * $inset, $Size - 2 * $inset)
    $bar = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#8caaee'))
    $g.FillRectangle($bar, $inset, $inset, $Size - 2 * $inset, [float]($Size * 0.14))
    $g.Dispose()
    $bmp.Save((Join-Path $out $Name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

New-Icon 150 'icon.png'     # Square150x150Logo
New-Icon 44 '44.png'        # Square44x44Logo
New-Icon 50 'store.png'     # Properties > Logo
