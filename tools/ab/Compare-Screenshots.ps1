<#
.SYNOPSIS
    Compares two bar screenshots outside a set of masked regions.
.DESCRIPTION
    A pixel differs when any colour channel differs by more than -ChannelTolerance. Masked
    rectangles (clock text, CPU/memory values, anything live) are ignored. The comparison
    passes when the share of differing pixels outside the masks is at most -MaxDiffPercent.
    Exit code 0 = pass, 1 = fail, 2 = bad input. Runs with pwsh on Windows.
.PARAMETER Reference
    Baseline image (PNG/BMP).
.PARAMETER Candidate
    Image to check.
.PARAMETER MaskFile
    JSON: { "masks": [ { "name": "clock", "x": 10, "y": 0, "width": 80, "height": 30 } ] }.
.PARAMETER ChannelTolerance
    Largest per-channel difference (0-255) still counted as equal. Absorbs anti-aliasing noise.
.PARAMETER MaxDiffPercent
    Largest share of unmasked pixels allowed to differ.
.PARAMETER DiffImage
    Optional path to write a PNG with differing pixels painted red.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Reference,
    [Parameter(Mandatory = $true)][string]$Candidate,
    [string]$MaskFile,
    [ValidateRange(0, 255)][int]$ChannelTolerance = 8,
    [double]$MaxDiffPercent = 0.1,
    [string]$DiffImage
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if (-not ('BarImageCompare' -as [type])) {
    Add-Type -ReferencedAssemblies @(
        [System.Drawing.Bitmap].Assembly.Location,
        [System.Drawing.Rectangle].Assembly.Location,
        'System.Runtime.dll'
    ) -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class BarImageCompare
{
    public sealed class Result
    {
        public long Compared;
        public long Different;
        public Bitmap Diff;
    }

    static byte[] Pixels(Bitmap bmp, out int stride)
    {
        var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
        var data = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        try
        {
            stride = data.Stride;
            var bytes = new byte[Math.Abs(data.Stride) * bmp.Height];
            Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
            return bytes;
        }
        finally { bmp.UnlockBits(data); }
    }

    public static Result Compare(Bitmap a, Bitmap b, int[][] masks, int tolerance, bool makeDiff)
    {
        int strideA, strideB;
        byte[] pa = Pixels(a, out strideA), pb = Pixels(b, out strideB);
        var result = new Result();
        byte[] diff = makeDiff ? new byte[Math.Abs(strideA) * a.Height] : null;
        for (int y = 0; y < a.Height; y++)
        {
            for (int x = 0; x < a.Width; x++)
            {
                int ia = y * strideA + x * 4, ib = y * strideB + x * 4;
                bool masked = false;
                foreach (var m in masks)
                {
                    if (x >= m[0] && x < m[0] + m[2] && y >= m[1] && y < m[1] + m[3]) { masked = true; break; }
                }
                bool different = false;
                if (!masked)
                {
                    result.Compared++;
                    for (int c = 0; c < 4; c++)
                    {
                        if (Math.Abs(pa[ia + c] - pb[ib + c]) > tolerance) { different = true; break; }
                    }
                    if (different) result.Different++;
                }
                if (diff != null)
                {
                    byte shade = (byte)(pa[ia] / 4 + 96);
                    diff[ia] = masked ? (byte)160 : (different ? (byte)0 : shade);
                    diff[ia + 1] = masked ? (byte)160 : (different ? (byte)0 : shade);
                    diff[ia + 2] = different ? (byte)255 : (masked ? (byte)160 : shade);
                    diff[ia + 3] = 255;
                }
            }
        }
        if (diff != null)
        {
            var bmp = new Bitmap(a.Width, a.Height, PixelFormat.Format32bppArgb);
            var data = bmp.LockBits(new Rectangle(0, 0, a.Width, a.Height), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(diff, 0, data.Scan0, diff.Length);
            bmp.UnlockBits(data);
            result.Diff = bmp;
        }
        return result;
    }
}
'@
}

function Read-Masks([string]$Path) {
    if (-not $Path) { return ,@() }
    $json = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $list = New-Object System.Collections.ArrayList
    foreach ($mask in @($json.masks)) {
        [void]$list.Add([int[]]@([int]$mask.x, [int]$mask.y, [int]$mask.width, [int]$mask.height))
    }
    return , ($list.ToArray([int[]]))
}

try {
    $refPath = (Resolve-Path -LiteralPath $Reference).Path
    $candPath = (Resolve-Path -LiteralPath $Candidate).Path
    $masks = Read-Masks $MaskFile
} catch {
    Write-Error $_
    exit 2
}

$ref = New-Object System.Drawing.Bitmap $refPath
$cand = New-Object System.Drawing.Bitmap $candPath
try {
    if ($ref.Width -ne $cand.Width -or $ref.Height -ne $cand.Height) {
        Write-Output ("FAIL size mismatch: reference {0}x{1}, candidate {2}x{3}" -f $ref.Width, $ref.Height, $cand.Width, $cand.Height)
        exit 1
    }
    $result = [BarImageCompare]::Compare($ref, $cand, [int[][]]$masks, $ChannelTolerance, [bool]$DiffImage)
    if ($DiffImage) {
        $result.Diff.Save($DiffImage, [System.Drawing.Imaging.ImageFormat]::Png)
        $result.Diff.Dispose()
    }
    $percent = if ($result.Compared -gt 0) { 100.0 * $result.Different / $result.Compared } else { 0.0 }
    $pass = $percent -le $MaxDiffPercent
    $verdict = if ($pass) { 'PASS' } else { 'FAIL' }
    Write-Output ('{0} {1} of {2} unmasked pixels differ ({3:N4}%, limit {4}%, channel tolerance {5}, {6} mask(s))' -f `
            $verdict, $result.Different, $result.Compared, $percent, $MaxDiffPercent, $ChannelTolerance, @($masks).Count)
    if ($pass) { exit 0 } else { exit 1 }
} finally {
    $ref.Dispose()
    $cand.Dispose()
}
