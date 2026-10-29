<#
.SYNOPSIS
    Definition-of-done check for wabar: format, layering, JSON subscripts, build and tests.
.DESCRIPTION
    Steps, in order:
      1. format     clang-format --dry-run -Werror over *.cpp/*.h in core, platform, widgets, spike
                    (with -Staged: only the staged versions of staged files, which is what
                    the pre-commit hook uses).
      2. layering   fails on an #include "..." against the layer arrow
                    spike -> widgets -> core -> platform
                    (with -Staged: only staged files, reading their staged content).
      3. json-subscript
                    heuristic grep for the const nlohmann::json operator[] in product code (core,
                    platform, widgets).
      4. configure  cmake --preset <Preset>       (skipped with -Fast)
      5. build      cmake --build --preset <Preset> (skipped with -Fast)
      6. test       ctest --test-dir build\<Preset> --output-on-failure --timeout 120 (skipped with -Fast)
    -Analyze replaces steps 4-6 with an isolated MSVC /analyze build and clang-tidy.
    Configure, build and test each run in their own cmd.exe after the matching vcvars*.bat, with
    VCPKG_ROOT=C:\vcpkg (override with the VCPKG_ROOT_OVERRIDE environment variable).
    Exits 1 if any step fails. Requires PowerShell 7 (pwsh).
.PARAMETER Fast
    Only the format, layering and JSON-subscript checks.
.PARAMETER Analyze
    Instead of ordinary build/tests, run MSVC /analyze and clang-tidy on production units in an
    isolated build\<Preset>-analyze directory. Requires LLVM clang-tidy.
.PARAMETER SkipFormat
    Skip the clang-format check.
.PARAMETER Staged
    Format- and layering-check only files staged in the index, reading their staged content.
.PARAMETER Preset
    CMake configure/build preset. Default arm64-debug.
.EXAMPLE
    pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -Fast
#>
[CmdletBinding()]
param(
    [switch]$Fast,
    [switch]$SkipFormat,
    [switch]$Staged,
    [switch]$Analyze,
    [ValidateSet('arm64-debug', 'arm64-release', 'x64-release')]
    [string]$Preset = 'arm64-debug'
)

Set-StrictMode -Version 2.0
# Not 'Stop': Windows PowerShell 5.1 turns native stderr under 2>&1 into terminating errors.
$ErrorActionPreference = 'Continue'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $repo
if ($Analyze -and ($Fast -or $Staged)) {
    Write-Error '-Analyze cannot be combined with -Fast or -Staged.'
    exit 1
}

$formatDirs = @('core', 'platform', 'widgets', 'spike')
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
        'if errorlevel 1 exit /b %errorlevel%',
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

# 2. layering --------------------------------------------------------------------------------
# All targets share the repo root as include directory, so CMake alone cannot stop a header from
# being included against the arrow (spike -> widgets -> core -> platform). The
# first matching folder prefix wins; its list holds the include prefixes it must not use.
Write-Step 'layering (#include directions)'
$sw = [Diagnostics.Stopwatch]::StartNew()
$layerRules = @(
    @{ Dir = 'platform/'; Deny = @('core/', 'widgets/', 'spike/') },
    @{ Dir = 'core/';     Deny = @('widgets/', 'spike/') },
    @{ Dir = 'widgets/';  Deny = @('spike/') }
)
$layerDirs = @('core', 'platform', 'widgets')
if ($Staged) {
    $layerFiles = @(git diff --cached --name-only --diff-filter=ACMR -- $layerDirs)
} else {
    $layerFiles = @(git ls-files --cached --others --exclude-standard -- $layerDirs)
}
$layerFiles = @($layerFiles | Where-Object { $_ -match '\.(cpp|h)$' } | Sort-Object -Unique)
$violations = New-Object System.Collections.Generic.List[string]
foreach ($f in $layerFiles) {
    $rule = $layerRules | Where-Object { $f.StartsWith($_.Dir) } | Select-Object -First 1
    if (-not $rule) { continue }
    if ($Staged) {
        $lines = @(git show ":$f" 2>$null)
    } elseif (Test-Path -LiteralPath $f) {
        $lines = @(Get-Content -LiteralPath $f)
    } else {
        continue
    }
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ("$($lines[$i])" -match '^\s*#\s*include\s+"([^"]+)"') {
            $inc = $Matches[1]
            foreach ($deny in $rule.Deny) {
                if ($inc.StartsWith($deny)) {
                    $violations.Add("${f}:$($i + 1): includes `"$inc`" ($($rule.Dir) must not include $deny)")
                    break
                }
            }
        }
    }
}
if ($violations.Count -eq 0) {
    Add-Result 'layering' 'PASS' "$($layerFiles.Count) files respect wabar (spike) -> widgets -> core -> platform" $sw.Elapsed.TotalSeconds
} else {
    Write-Host 'Includes against the layering arrow (see AGENTS.md, Layering):'
    $violations | ForEach-Object { Write-Host "  $_" }
    Add-Result 'layering' 'FAIL' "$($violations.Count) include(s) against the arrow" $sw.Elapsed.TotalSeconds
}

# 3. json-subscript ---------------------------------------------------------------------------
# operator[] on a const nlohmann::json with a missing key is undefined behaviour (AGENTS.md, error
# handling). Types are invisible to a grep, so this is a heuristic over files that mention "json":
#   flagged  a string-literal subscript that is not assigned to (x["k"] ... with no "] =" after it),
#            an iterator value subscript (it.value()[...]) and a dereference subscript ((*p)[...]).
#   missed   subscripts with a variable key on a named object (obj[key]), and any json use in a file
#            that never spells "json" (for example through an alias header).
#   false +  a read through a std::map/QMap with a literal key in such a file; use at()/find() or
#            end the line with the comment "json-subscript-ok".
Write-Step 'json-subscript (const nlohmann::json operator[] heuristic)'
$sw = [Diagnostics.Stopwatch]::StartNew()
$productDirs = @('core', 'platform', 'widgets')
if ($Staged) {
    $jsonFiles = @(git diff --cached --name-only --diff-filter=ACMR -- $productDirs)
} else {
    $jsonFiles = @(git ls-files --cached --others --exclude-standard -- $productDirs)
}
$jsonFiles = @($jsonFiles | Where-Object { $_ -match '\.(cpp|h)$' } | Sort-Object -Unique)
$literalRead = '[\w\)\]]\s*\[\s*"[^"\\]*"\s*\]'
$assigned = '\]\s*=(?!=)'
$otherRead = '\.value\(\)\s*\[|\(\*\w+\)\s*\['
$hits = New-Object System.Collections.Generic.List[string]
foreach ($f in $jsonFiles) {
    if ($Staged) {
        $lines = @(git show ":$f" 2>$null)
    } elseif (Test-Path -LiteralPath $f) {
        $lines = @(Get-Content -LiteralPath $f)
    } else {
        continue
    }
    if (-not ($lines -match 'json')) { continue }
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = "$($lines[$i])"
        if ($line -match '^\s*//' -or $line -match 'json-subscript-ok') { continue }
        $code = ($line -replace '\s//.*$', '')
        if ((($code -match $literalRead) -and ($code -notmatch $assigned)) -or ($code -match $otherRead)) {
            $hits.Add("${f}:$($i + 1): $($line.Trim())")
        }
    }
}
if ($hits.Count -eq 0) {
    Add-Result 'json-subscript' 'PASS' "$($jsonFiles.Count) files scanned" $sw.Elapsed.TotalSeconds
} else {
    Write-Host 'Possible const nlohmann::json operator[] (use find(), at() or value()):'
    $hits | ForEach-Object { Write-Host "  $_" }
    Add-Result 'json-subscript' 'FAIL' "$($hits.Count) suspicious subscript(s)" $sw.Elapsed.TotalSeconds
}

# 4-6. configure, build, test (or isolated static analysis) ------------------------------------
if ($Analyze) {
    $vcvars = Get-VcvarsScript $Preset
    $analysisDir = Join-Path $repo "build\$Preset-analyze"
    Write-Step 'MSVC /analyze (production targets, isolated build)'
    $sw = [Diagnostics.Stopwatch]::StartNew()
    if (-not (Test-Path $vcvars)) {
        Add-Result 'msvc-analyze' 'FAIL' "missing $vcvars (set YASB_VCVARS_DIR)" 0
        Add-Result 'clang-tidy' 'SKIP' 'configure failed' 0
    } else {
        $code = Invoke-InVsEnv 'analyze-configure' $vcvars "cmake --preset $Preset -B `"$analysisDir`" -DYASB_ANALYZE=ON -DBUILD_TESTING=OFF"
        if ($code -ne 0) {
            Add-Result 'msvc-analyze' 'FAIL' "configure exit $code" $sw.Elapsed.TotalSeconds
            Add-Result 'clang-tidy' 'SKIP' 'configure failed' 0
        } else {
            $log = Join-Path $analysisDir 'msvc-analyze.log'
            # Clean rebuild: incremental no-op builds do not run the analyzer.
            $command = @(
                "cmake --build `"$analysisDir`" --clean-first > `"$log`" 2>&1",
                'set rc=%errorlevel%',
                "type `"$log`"",
                'exit /b %rc%'
            ) -join "`r`n"
            $code = Invoke-InVsEnv 'msvc-analyze' $vcvars $command
            $findings = @(Get-Content $log | Where-Object { $_ -match 'warning C\d{4,5}:' }).Count
            $status = 'PASS'
            if ($code -ne 0 -or $findings -gt 0) { $status = 'FAIL' }
            Add-Result 'msvc-analyze' $status "$findings diagnostics; exit $code; $log" $sw.Elapsed.TotalSeconds

            Write-Step 'clang-tidy (production compile database entries)'
            $sw = [Diagnostics.Stopwatch]::StartNew()
            $wabar = Get-Command clang-tidy -ErrorAction SilentlyContinue
            $wabarExe = 'C:\Program Files\LLVM\bin\clang-tidy.exe'
            if ($wabar) { $wabarExe = $wabar.Source }
            if (-not (Test-Path $wabarExe)) {
                Add-Result 'clang-tidy' 'FAIL' 'clang-tidy not found on PATH or in C:\Program Files\LLVM\bin' 0
            } elseif ($code -ne 0) {
                Add-Result 'clang-tidy' 'FAIL' 'MSVC build failed; generated Qt headers may be unavailable' 0
            } else {
                $entries = Get-Content (Join-Path $analysisDir 'compile_commands.json') -Raw | ConvertFrom-Json
                # clang-cl does not accept MSVC's analyzer/anglebrackets switches. Keep the
                # original database for MSVC; /external:I still marks dependency headers.
                $wabarDir = Join-Path $analysisDir 'clang-tidy'
                New-Item -ItemType Directory -Force -Path $wabarDir | Out-Null
                foreach ($entry in $entries) {
                    $entry.command = $entry.command -replace '(?<=\s)/analyze(?=\s)', '' `
                        -replace '(?<=\s)/analyze:external-(?=\s)', '' `
                        -replace '(?<=\s)/external:anglebrackets(?=\s)', ''
                }
                $entries | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $wabarDir 'compile_commands.json')
                $units = @($entries | Where-Object {
                    $relative = [IO.Path]::GetRelativePath($repo, $_.file).Replace('\', '/')
                    $relative -match '^(core|platform|widgets)/.*\.cpp$'
                } | ForEach-Object { $_.file } | Sort-Object -Unique)
                $commands = New-Object System.Collections.Generic.List[string]
                $commands.Add('set failures=0')
                $log = Join-Path $analysisDir 'clang-tidy.log'
                $commands.Add("type nul > `"$log`"")
                $triple = 'aarch64-pc-windows-msvc'
                if ($Preset -eq 'x64-release') { $triple = 'x86_64-pc-windows-msvc' }
                foreach ($unit in $units) {
                    $commands.Add("echo Analyzing $unit >> `"$log`"")
                    # Qt signals named event() intentionally do not override QObject::event().
                    $commands.Add("`"$wabarExe`" -p `"$wabarDir`" --extra-arg=--target=$triple --extra-arg=-Wno-overloaded-virtual `"$unit`" >> `"$log`" 2>&1")
                    $commands.Add('if errorlevel 1 set /a failures+=1 >nul')
                }
                $commands.Add("type `"$log`"")
                $commands.Add('exit /b %failures%')
                if ($units.Count -eq 0) {
                    Add-Result 'clang-tidy' 'FAIL' 'no production compilation units found' 0
                } else {
                    $code = Invoke-InVsEnv 'clang-tidy' $vcvars ($commands -join "`r`n")
                    $findings = @(Get-Content $log | Where-Object { $_ -match '(^|:\d+:\d+: )(warning|error|fatal error):' }).Count
                    $status = 'PASS'
                    if ($code -ne 0 -or $findings -gt 0) { $status = 'FAIL' }
                    Add-Result 'clang-tidy' $status "$($units.Count) units; $findings diagnostics; $code failed units; $log" $sw.Elapsed.TotalSeconds
                }
            }
        }
    }
} elseif ($Fast) {
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
        $code = Invoke-InVsEnv 'configure' $vcvars "cmake --preset $Preset -DYASB_ANALYZE=OFF"
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
