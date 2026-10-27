# Exercises Compare-Screenshots.ps1 on synthetic images. Run: pwsh -File tools\ab\tests\Test-CompareScreenshots.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$script = Join-Path (Split-Path -Parent $PSScriptRoot) 'Compare-Screenshots.ps1'
$work = Join-Path $PSScriptRoot ('.work-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$failures = 0

function New-Image([string]$Path, [scriptblock]$Paint) {
    $bmp = New-Object System.Drawing.Bitmap 200, 40
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(30, 30, 40))
    & $Paint $g
    $g.Dispose()
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

function Invoke-Compare([string[]]$Arguments) {
    $output = & pwsh -NoProfile -ExecutionPolicy Bypass -File $script @Arguments
    return @{ Code = $LASTEXITCODE; Text = ($output -join ' ') }
}

function Assert-Exit([string]$Name, [int]$Expected, $Actual) {
    if ($Actual.Code -eq $Expected) {
        Write-Output "ok   $Name"
    } else {
        Write-Output "FAIL $Name (exit $($Actual.Code), expected $Expected): $($Actual.Text)"
        $script:failures++
    }
}

try {
    $blob = { param($g) $g.FillRectangle([System.Drawing.Brushes]::White, 150, 5, 40, 30) }
    $a = Join-Path $work 'a.png'; $b = Join-Path $work 'b.png'; $c = Join-Path $work 'c.png'
    New-Image $a { param($g) }
    New-Image $b { param($g) }
    New-Image $c $blob
    $maskInside = Join-Path $work 'inside.json'
    $maskOutside = Join-Path $work 'outside.json'
    '{ "masks": [ { "name": "blob", "x": 140, "y": 0, "width": 60, "height": 40 } ] }' | Set-Content $maskInside
    '{ "masks": [ { "name": "elsewhere", "x": 0, "y": 0, "width": 50, "height": 40 } ] }' | Set-Content $maskOutside

    Assert-Exit 'identical images pass' 0 (Invoke-Compare @('-Reference', $a, '-Candidate', $b))
    Assert-Exit 'difference inside mask passes' 0 (Invoke-Compare @('-Reference', $a, '-Candidate', $c, '-MaskFile', $maskInside))
    Assert-Exit 'difference outside mask fails' 1 (Invoke-Compare @('-Reference', $a, '-Candidate', $c, '-MaskFile', $maskOutside))
    Assert-Exit 'difference without mask fails' 1 (Invoke-Compare @('-Reference', $a, '-Candidate', $c))
    Assert-Exit 'large tolerance absorbs difference' 0 (Invoke-Compare @('-Reference', $a, '-Candidate', $c, '-ChannelTolerance', '255'))
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures -gt 0) { Write-Output "$failures failure(s)"; exit 1 }
Write-Output 'all passed'
