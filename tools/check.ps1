<#
.SYNOPSIS
    Definition-of-done check for yasb-native: format, generated files, build and tests.
.DESCRIPTION
    Steps, in order:
      1. format     clang-format --dry-run -Werror over *.cpp/*.h in app, core, platform, widgets,
                    tests and tools/yasbc-native (with -Staged: only the staged versions of staged
                    files, which is what the pre-commit hook uses).
      2. generated  runs core/config/options/generate_options.py into a scratch copy under build\
                    and compares the output with core/config/options/*.h. The working tree is never
                    rewritten.
      3. configure  cmake --preset <Preset>       (skipped with -Fast)
      4. build      cmake --build --preset <Preset> (skipped with -Fast)
      5. test       ctest --test-dir build\<Preset> --output-on-failure --timeout 120 (skipped with -Fast)
    Configure, build and test each run in their own cmd.exe after the matching vcvars*.bat, with
    VCPKG_ROOT=C:\vcpkg (override with the VCPKG_ROOT_OVERRIDE environment variable).
    Exits 1 if any step fails. Compatible with Windows PowerShell 5.1.
.PARAMETER Fast
    Only the format and generated-file checks.
.PARAMETER SkipFormat
    Skip the clang-format check.
.PARAMETER Staged
    Format-check only files staged in the index, reading their staged content.
.PARAMETER Preset
    CMake configure/build preset. Default arm64-debug.
.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -Fast
#>
[CmdletBinding()]
param(
    [switch]$Fast,
    [switch]$SkipFormat,
    [switch]$Staged,
    [ValidateSet('arm64-debug', 'arm64-release', 'x64-release')]
    [string]$Preset = 'arm64-debug'
)

Set-StrictMode -Version 2.0
# Not 'Stop': Windows PowerShell 5.1 turns native stderr under 2>&1 into terminating errors.
$ErrorActionPreference = 'Continue'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $repo

$formatDirs = @('app', 'core', 'platform', 'widgets', 'tests', 'tools/yasbc-native')
$results = New-Object System.Collections.Generic.List[object]

function Add-Result([string]$Step, [string]$Status, [string]$Detail, [double]$Seconds) {
    $results.Add([pscustomobject]@{
        Step    = $Step
        Status  = $Status
        Seconds = [math]::Round($Seconds, 1)
        Detail  = $Detail
    })
}

function Write-Step([string]$Name) {
    Write-Host ''
    Write-Host "==> $Name" -ForegroundColor Cyan
}

function Find-ClangFormat {
    $cmd = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $fallback = 'C:\Program Files\LLVM\bin\clang-format.exe'
    if (Test-Path $fallback) { return $fallback }
    return $null
}

function Find-Python {
    # Returns the command prefix (exe + leading args) of a working Python 3.
    $candidates = @(@('py', '-3'), @('python'), @('python3'))
    foreach ($c in $candidates) {
        if (-not (Get-Command $c[0] -ErrorAction SilentlyContinue)) { continue }
        $exe = $c[0]
        $pre = @()
        if ($c.Count -gt 1) { $pre = $c[1..($c.Count - 1)] }
        $out = & $exe @pre -c 'import sys; print(sys.version_info[0])' 2>$null
        if ($LASTEXITCODE -eq 0 -and "$out".Trim() -eq '3') { return , (@($exe) + $pre) }
    }
    return $null
}

function Get-VcvarsScript([string]$PresetName) {
    $dir = $env:YASB_VCVARS_DIR
    if (-not $dir) {
        $dir = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build'
    }
    $hostArch = $env:PROCESSOR_ARCHITEW6432
    if (-not $hostArch) { $hostArch = $env:PROCESSOR_ARCHITECTURE }
    $armHost = $hostArch -eq 'ARM64'
    if ($PresetName -like 'arm64-*') {
        if ($armHost) { $bat = 'vcvarsarm64.bat' } else { $bat = 'vcvarsamd64_arm64.bat' }
    } else {
        if ($armHost) { $bat = 'vcvarsarm64_amd64.bat' } else { $bat = 'vcvars64.bat' }
    }
    return (Join-Path $dir $bat)
}

function Invoke-InVsEnv([string]$Name, [string]$Vcvars, [string]$Command) {
    $vcpkg = $env:VCPKG_ROOT_OVERRIDE
    if (-not $vcpkg) { $vcpkg = 'C:\vcpkg' }
    # A .cmd file avoids the cmd.exe quoting differences between PowerShell 5.1 and 7.
    # vcvars resets VCPKG_ROOT to the bundled vcpkg, so set it afterwards.
    $script = Join-Path $repo "build\check-$Name.cmd"
    New-Item -ItemType Directory -Force -Path (Split-Path $script) | Out-Null
    $lines = @(
        '@echo off',
        "call `"$Vcvars`" >nul 2>nul",
        "set `"VCPKG_ROOT=$vcpkg`"",
        $Command,
        'exit /b %errorlevel%'
    )
    [IO.File]::WriteAllText($script, (($lines -join "`r`n") + "`r`n"))
    & $script | Out-Host
    $code = $LASTEXITCODE
    Remove-Item -Force $script
    return $code
}

# 1. format ----------------------------------------------------------------------------------
if ($SkipFormat) {
    Add-Result 'format' 'SKIP' '-SkipFormat' 0
} else {
    Write-Step 'format (clang-format --dry-run -Werror)'
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $clang = Find-ClangFormat
    if (-not $clang) {
        Add-Result 'format' 'FAIL' 'clang-format not found on PATH or in C:\Program Files\LLVM\bin' $sw.Elapsed.TotalSeconds
    } else {
        if ($Staged) {
            $files = @(git diff --cached --name-only --diff-filter=ACMR -- $formatDirs)
        } else {
            $files = @(git ls-files --cached --others --exclude-standard -- $formatDirs)
        }
        $files = @($files | Where-Object { $_ -match '\.(cpp|h)$' } | Sort-Object -Unique)
        $root = ''
        if ($Staged -and $files.Count -gt 0) {
            # Check the index content, not the working tree: copy staged blobs under build\ (byte
            # exact, .clang-format is still found by walking up to the repo root).
            $root = 'build/check-staged/'
            if (Test-Path $root) { Remove-Item -Recurse -Force $root }
            git checkout-index -f "--prefix=$root" -- $files
        }
        $bad = New-Object System.Collections.Generic.List[string]
        $diagnostics = 0
        foreach ($f in $files) {
            $path = "$root$f"
            if (-not (Test-Path -LiteralPath $path)) { continue }
            $out = & $clang --dry-run -Werror $path 2>&1
            if ($LASTEXITCODE -ne 0) {
                $bad.Add($f)
                $diagnostics += @($out | Where-Object { "$_" -match '(warning|error): code should be clang-formatted' }).Count
            }
        }
        if ($root -and (Test-Path $root)) { Remove-Item -Recurse -Force $root }
        $scope = 'files'
        if ($Staged) { $scope = 'staged files' }
        if ($bad.Count -eq 0) {
            Add-Result 'format' 'PASS' "$($files.Count) $scope clean" $sw.Elapsed.TotalSeconds
        } else {
            Write-Host "Files needing clang-format ($($bad.Count)):"
            $bad | ForEach-Object { Write-Host "  $_" }
            Add-Result 'format' 'FAIL' "$($bad.Count) of $($files.Count) $scope unformatted, $diagnostics diagnostics (fix: clang-format -i <file>)" $sw.Elapsed.TotalSeconds
        }
    }
}

# 2. generated -------------------------------------------------------------------------------
Write-Step 'generated (core/config/options vs generate_options.py)'
$sw = [Diagnostics.Stopwatch]::StartNew()
$python = Find-Python
if (-not $python) {
    Add-Result 'generated' 'FAIL' 'Python 3 not found (tried py -3, python, python3)' $sw.Elapsed.TotalSeconds
} else {
    $scratch = Join-Path $repo 'build\check-generated'
    if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
    $genDir = Join-Path $scratch 'core\config\options'
    New-Item -ItemType Directory -Force -Path $genDir | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $scratch 'schema') | Out-Null
    Copy-Item 'core\config\options\generate_options.py' $genDir
    Copy-Item 'schema\schema.json' (Join-Path $scratch 'schema')
    $pyExe = $python[0]
    $pyArgs = @()
    if ($python.Count -gt 1) { $pyArgs = $python[1..($python.Count - 1)] }
    $genOut = & $pyExe @pyArgs -I (Join-Path $genDir 'generate_options.py') 2>&1
    if ($LASTEXITCODE -ne 0) {
        $genOut | ForEach-Object { Write-Host "  $_" }
        Add-Result 'generated' 'FAIL' "generate_options.py exited $LASTEXITCODE" $sw.Elapsed.TotalSeconds
    } else {
        $expected = @(Get-ChildItem $genDir -Filter '*.h' | ForEach-Object { $_.Name })
        $actual = @(Get-ChildItem 'core\config\options' -Filter '*.h' | ForEach-Object { $_.Name })
        $drift = New-Object System.Collections.Generic.List[string]
        foreach ($name in $expected) {
            $mine = Join-Path 'core\config\options' $name
            if (-not (Test-Path $mine)) { $drift.Add("$name (missing)"); continue }
            $a = (Get-FileHash -Algorithm SHA256 (Join-Path $genDir $name)).Hash
            $b = (Get-FileHash -Algorithm SHA256 $mine).Hash
            if ($a -ne $b) { $drift.Add("$name (differs)") }
        }
        foreach ($name in $actual) {
            if ($expected -notcontains $name) { $drift.Add("$name (not generated)") }
        }
        if ($drift.Count -eq 0) {
            Remove-Item -Recurse -Force $scratch
            Add-Result 'generated' 'PASS' "$($expected.Count) headers match" $sw.Elapsed.TotalSeconds
        } else {
            Write-Host 'Generated headers drift from generate_options.py:'
            $drift | ForEach-Object { Write-Host "  $_" }
            Write-Host "Expected output kept in $genDir. Fix: python core\config\options\generate_options.py"
            Add-Result 'generated' 'FAIL' "$($drift.Count) header(s) drift" $sw.Elapsed.TotalSeconds
        }
    }
}

# 3-5. configure, build, test ----------------------------------------------------------------
if ($Fast) {
    Add-Result 'configure' 'SKIP' '-Fast' 0
    Add-Result 'build' 'SKIP' '-Fast' 0
    Add-Result 'test' 'SKIP' '-Fast' 0
} else {
    $vcvars = Get-VcvarsScript $Preset
    $buildOk = $false
    if (-not (Test-Path $vcvars)) {
        Add-Result 'configure' 'FAIL' "missing $vcvars (set YASB_VCVARS_DIR)" 0
        Add-Result 'build' 'SKIP' 'configure failed' 0
    } else {
        Write-Step "configure (cmake --preset $Preset)"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $code = Invoke-InVsEnv 'configure' $vcvars "cmake --preset $Preset"
        if ($code -ne 0) {
            Add-Result 'configure' 'FAIL' "exit $code" $sw.Elapsed.TotalSeconds
            Add-Result 'build' 'SKIP' 'configure failed' 0
        } else {
            Add-Result 'configure' 'PASS' (Split-Path $vcvars -Leaf) $sw.Elapsed.TotalSeconds
            Write-Step "build (cmake --build --preset $Preset)"
            $sw = [Diagnostics.Stopwatch]::StartNew()
            $code = Invoke-InVsEnv 'build' $vcvars "cmake --build --preset $Preset"
            if ($code -ne 0) {
                Add-Result 'build' 'FAIL' "exit $code" $sw.Elapsed.TotalSeconds
            } else {
                Add-Result 'build' 'PASS' '' $sw.Elapsed.TotalSeconds
                $buildOk = $true
            }
        }
    }
    if (-not $buildOk) {
        Add-Result 'test' 'SKIP' 'build failed' 0
    } else {
        Write-Step "test (ctest --test-dir build\$Preset)"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        # ctest ships with the Visual Studio CMake, so it also runs inside the vcvars environment.
        $ctestLog = Join-Path $repo "build\$Preset\check-ctest.log"
        $ctestCmd = @(
            "ctest --test-dir `"build\$Preset`" --output-on-failure --timeout 120 > `"$ctestLog`" 2>&1",
            'set rc=%errorlevel%',
            "type `"$ctestLog`"",
            'exit /b %rc%'
        ) -join "`r`n"
        $code = Invoke-InVsEnv 'test' $vcvars $ctestCmd
        $detail = ''
        if (Test-Path $ctestLog) {
            $summary = @(Get-Content $ctestLog | Where-Object { $_ -match 'tests passed' } | Select-Object -Last 1)
            if ($summary.Count -gt 0) { $detail = "$($summary[0])".Trim() }
        }
        if (-not $detail) { $detail = "exit $code" }
        if ($code -ne 0) {
            Add-Result 'test' 'FAIL' $detail $sw.Elapsed.TotalSeconds
        } else {
            Add-Result 'test' 'PASS' $detail $sw.Elapsed.TotalSeconds
        }
    }
}

# summary ------------------------------------------------------------------------------------
Write-Host ''
Write-Host "check.ps1 summary (preset $Preset)" -ForegroundColor Cyan
$results | Format-Table -AutoSize -Wrap | Out-String -Width 200 | Write-Host
$failed = @($results | Where-Object { $_.Status -eq 'FAIL' })
if ($failed.Count -gt 0) {
    Write-Host "FAILED: $(($failed | ForEach-Object { $_.Step }) -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host 'All checks passed.' -ForegroundColor Green
exit 0
