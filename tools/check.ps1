<#
.SYNOPSIS
    Definition-of-done check for waku: format, structure, JSON subscripts, build and tests.
.DESCRIPTION
    Steps, in order:
      1. format     clang-format --dry-run -Werror over *.cpp/*.h in src and tests (with -Staged:
                    only the staged versions of staged files, which is what the pre-commit hook
                    uses).
      2. structure  rules S1-S8 of the waku architecture (DEPENDS read from the waku_feature(
                    calls in src/features/*/CMakeLists.txt); every violation under src/ and tests/
                    fails the step. With -Staged: per-file rules on staged files, global rules on
                    the index.
      3. json-subscript
                    heuristic grep for the const nlohmann::json operator[] in product code (src,
                    tests).
      4. configure  cmake --preset <Preset>       (skipped with -Fast)
      5. build      cmake --build --preset <Preset> (skipped with -Fast)
      6. qmllint    cmake --build --preset <Preset> --target all_qmllint; fails when the warning
                    count rises above tools\qmllint-baseline.txt (skipped with -Fast)
      7. test       ctest --test-dir build\<Preset> --output-on-failure --timeout 120 -j (skipped with -Fast)
    -Analyze replaces steps 4-7 with an isolated MSVC /analyze build and clang-tidy.
    Configure, build and test each run in their own cmd.exe after the matching vcvars*.bat, with
    VCPKG_ROOT=C:cpkg (override with the VCPKG_ROOT_OVERRIDE environment variable).
    Exits 1 if any step fails. Requires PowerShell 7 (pwsh).
.PARAMETER Fast
    Only the format, structure and JSON-subscript checks.
.PARAMETER Analyze
    Instead of ordinary build/tests, run MSVC /analyze and clang-tidy on production units in an
    isolated build\<Preset>-analyze directory. Requires LLVM clang-tidy.
.PARAMETER SkipFormat
    Skip the clang-format check.
.PARAMETER Staged
    Format- and structure-check only files staged in the index, reading their staged content.
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

$formatDirs = @('src', 'tests')
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
    $dir = $env:WAKU_VCVARS_DIR
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

# 2. structure ------------------------------------------------------------------------------
# Rules S1-S8 (docs/architecture.md). The DEPENDS
# graph comes from the waku_feature( calls in src/features/*/CMakeLists.txt, read with a regex, so
# no build tree is needed. Units: kernel, control, host, ui, features/<f>, quickshell, app/<exe>; tests/ (the
# top-level folder) holds the test main and the integration tests.
# Mode: ENFORCING (any violation fails the step).
# Decisions beyond the proposal: S3 and S4 also allow */tests/** and tests/ (tests build QML
# engines and Win32 fakes); S2 requires includes rooted at src/ (a bare "x.h" fails, "x.moc" is
# fine); S1 also requires each .cpp/.qml of a feature to be named in its CMakeLists.txt and the
# NAME to equal the folder name.
Write-Step 'structure (S1-S8)'
$sw = [Diagnostics.Stopwatch]::StartNew()
$structureRules = [ordered]@{
    S1 = 'every source under src/ belongs to exactly one unit and is declared by it'
    S2 = 'quoted includes: own unit, kernel/, root headers of control/ and of DEPENDS features (their tests also host/testing/ and the testing/ of DEPENDS); host also control/; quickshell also control/, host/qml/ and the root and qml/ headers of any feature; app/ and tests/ free'
    S3 = 'Windows SDK and C++/WinRT headers only in */win32/**, features/tray/hook/, app/, tests'
    S4 = 'QtQml/QtQuick includes and QML_* macros only in */qml/**, */testing/**, app/, tests; QtWidgets nowhere'
    S5 = 'only */tests/**, */testing/** and tests/ include */testing/** (features: own, DEPENDS and host/testing/)'
    S6 = 'a feature''s .qml uses services (S.<Type>) and widgets of its own feature or its DEPENDS only'
    S7 = 'QML-facing header basenames unique across the services module; .qml basenames unique per module'
    S8 = 'DEPENDS name existing features and form an acyclic graph'
}
$windowsHeaders = '^(windows|winrt/.*|shlobj|shellapi|shobjidl|shobjidl_core|shlwapi|shldisp|exdisp|' +
    'propsys|propkey|propvarutil|propidl|objbase|combaseapi|ole2|ocidl|unknwn|initguid|winternl|' +
    'tlhelp32|psapi|powrprof|pdh|mmdeviceapi|endpointvolume|devicetopology|functiondiscoverykeys_devpkey|' +
    'mmsystem|ks|ksproxy|dwmapi|uxtheme|appmodel|ws2tcpip|winsock2|wlanapi|winhttp|winioctl|setupapi|' +
    'cfgmgr32|devguid|bluetoothapis|batclass|security|sddl|physicalmonitorenumerationapi|' +
    'lowlevelmonitorconfigurationapi|highlevelmonitorconfigurationapi|notificationactivationcallback|' +
    'wtsapi32|winreg|winuser|versionhelpers|wincodec|roapi|wrl/.*|qt_windows)\.h$|^winrt/'
$qmlIncludes = '^(QQml\w*|QQuick\w*|QJSEngine|QJSValue|QtQml/.*|QtQuick/.*)$'
$qmlMacros = '\bQML_(ELEMENT|NAMED_ELEMENT|SINGLETON|UNCREATABLE|ANONYMOUS)\b'
$widgetsIncludes = '^(QWidget|QApplication|QtWidgets/.*)$'

function Get-CheckLines([string]$Path) {
    if ($Staged) { return @(git show ":$Path" 2>$null) }
    if (Test-Path -LiteralPath $Path) { return @(Get-Content -LiteralPath $Path) }
    return @()
}

# The whole tree (index with -Staged) for the global rules; the checked set for per-file rules.
if ($Staged) {
    $treeFiles = @(git ls-files --cached -- src tests)
    $checkFiles = @(git diff --cached --name-only --diff-filter=ACMR -- src tests)
} else {
    $treeFiles = @(git ls-files --cached --others --exclude-standard -- src tests)
    $checkFiles = $treeFiles
}
$treeFiles = @($treeFiles | Where-Object { $_ -and ($Staged -or (Test-Path -LiteralPath $_)) } | Sort-Object -Unique)
$checkFiles = @($checkFiles | Where-Object { $_ -match '\.(cpp|h|qml)$' } | Sort-Object -Unique)

$enforced = New-Object System.Collections.Generic.List[string]
function Add-StructureViolation([string]$Rule, [string]$File, [string]$Text) {
    $enforced.Add("${Rule} ${File}: $Text")
}

# S8 and the DEPENDS map: features[<f>] = @{ Depends = @(...); CMake = <text> }.
$features = @{}
foreach ($cm in @($treeFiles | Where-Object { $_ -match '^src/features/([^/]+)/CMakeLists\.txt$' })) {
    $folder = ($cm -split '/')[2]
    $text = (Get-CheckLines $cm) -join "`n"
    $text = ($text -split "`n" | ForEach-Object { $_ -replace '#.*$', '' }) -join "`n"
    $m = [regex]::Match($text, '(?s)waku_feature\s*\((.*?)\)')
    if (-not $m.Success) {
        Add-StructureViolation 'S1' $cm 'no waku_feature( call'
        continue
    }
    $tokens = @($m.Groups[1].Value -split '\s+' | Where-Object { $_ })
    $name = $null; $depends = @(); $section = ''
    foreach ($t in $tokens) {
        if ($t -cmatch '^(NAME|DEPENDS|SOURCES|WIN32_SOURCES|QML_SOURCES|QML_FILES|TESTS|SYSTEM_LIBS)$') {
            $section = $t; continue
        }
        if ($section -eq 'NAME') { $name = $t; $section = '' }
        elseif ($section -eq 'DEPENDS') { $depends += $t }
    }
    if ($name -ne $folder) {
        Add-StructureViolation 'S1' $cm "waku_feature NAME '$name' differs from the folder '$folder'"
    }
    $features[$folder] = @{ Depends = $depends; CMake = $m.Groups[1].Value }
}
foreach ($f in $features.Keys) {
    foreach ($d in $features[$f].Depends) {
        if (-not $features.ContainsKey($d)) {
            Add-StructureViolation 'S8' "src/features/$f/CMakeLists.txt" "DEPENDS $d is not a feature"
        }
    }
}
$visitState = @{}
function Test-FeatureCycle([string]$F, [string[]]$Path) {
    if ($visitState[$F] -eq 2) { return }
    if ($visitState[$F] -eq 1) {
        Add-StructureViolation 'S8' "src/features/$F/CMakeLists.txt" "DEPENDS cycle: $(($Path + $F) -join ' -> ')"
        return
    }
    $visitState[$F] = 1
    foreach ($d in $features[$F].Depends) {
        if ($features.ContainsKey($d)) { Test-FeatureCycle $d ($Path + $F) }
    }
    $visitState[$F] = 2
}
foreach ($f in $features.Keys) { Test-FeatureCycle $f @() }

# Unit of a path under src/: kernel, control, host, ui, features/<f>, quickshell, app/<exe>; $null if none.
function Get-Unit([string]$File) {
    if ($File -match '^src/(kernel|control|host|ui|quickshell)/') { return $Matches[1] }
    if ($File -match '^src/features/([^/]+)/.+') { return "features/$($Matches[1])" }
    if ($File -match '^src/app/([^/]+)/.+') { return "app/$($Matches[1])" }
    return $null
}

# S7 and the QML type map for S6: type name -> feature.
$qmlOwner = @{}
$serviceOwner = @{}
$headerSeen = @{}
$servicesHeaders = @($treeFiles | Where-Object { $_ -match '^src/.+/qml/[^/]+\.h$' })
foreach ($h in $servicesHeaders) {
    $base = Split-Path $h -Leaf
    if ($headerSeen.ContainsKey($base)) {
        Add-StructureViolation 'S7' $h "header basename $base also used by $($headerSeen[$base])"
    } else {
        $headerSeen[$base] = $h
    }
    if ($h -match '^src/features/([^/]+)/qml/') {
        $owner = $Matches[1]
        $class = $null
        foreach ($line in (Get-CheckLines $h)) {
            if ("$line" -match '^\s*class\s+(?:\w+\s+)?(\w+)\s*(?:final\s*)?[:{]') { $class = $Matches[1] }
            if ("$line" -match 'QML_NAMED_ELEMENT\(\s*(\w+)\s*\)') { $serviceOwner[$Matches[1]] = $owner }
            elseif ("$line" -match '\bQML_ELEMENT\b' -and $class) { $serviceOwner[$class] = $owner }
        }
    }
}
$moduleQml = @{
    'waku.widgets' = @($treeFiles | Where-Object { $_ -match '^src/.+\.qml$' -and $_ -notmatch '^src/(host|quickshell)/' })
    'waku'         = @($treeFiles | Where-Object { $_ -match '^src/host/.+\.qml$' })
    'Quickshell'    = @($treeFiles | Where-Object { $_ -match '^src/quickshell/.+\.qml$' })
}
foreach ($module in $moduleQml.Keys) {
    $seen = @{}
    foreach ($q in $moduleQml[$module]) {
        $base = Split-Path $q -Leaf
        if ($seen.ContainsKey($base)) {
            Add-StructureViolation 'S7' $q "$base also in $($seen[$base]) (module $module)"
        } else {
            $seen[$base] = $q
        }
        if ($q -match '^src/features/([^/]+)/qml/') { $qmlOwner[[IO.Path]::GetFileNameWithoutExtension($q)] = $Matches[1] }
    }
}

# Per-file rules.
foreach ($f in $checkFiles) {
    $isSrc = $f.StartsWith('src/')
    $isTests = ($f -match '(^|/)tests/')
    $unit = Get-Unit $f
    $feature = $null
    if ($unit -and $unit.StartsWith('features/')) { $feature = $unit.Substring(9) }

    if ($isSrc) {
        # S1
        if (-not $unit) {
            Add-StructureViolation 'S1' $f 'belongs to no unit (kernel, control, host, ui, features/<f>, quickshell, app/<exe>)'
        } elseif ($feature -and -not $features.ContainsKey($feature)) {
            Add-StructureViolation 'S1' $f "features/$feature has no CMakeLists.txt with waku_feature("
        } elseif ($feature -and $f -match '\.(cpp|qml)$') {
            $rel = $f.Substring("src/features/$feature/".Length)
            if ($features[$feature].CMake -notmatch "(^|\s)$([regex]::Escape($rel))(\s|$)") {
                Add-StructureViolation 'S1' $f "not declared in src/features/$feature/CMakeLists.txt"
            }
        }
    }

    $lines = @(Get-CheckLines $f)
    if ($f -match '\.qml$') {
        # S6
        if (-not $feature) { continue }
        $allowed = @($feature) + @($features[$feature].Depends)
        for ($i = 0; $i -lt $lines.Count; $i++) {
            # Comments and string literals are text, not type uses (a label "Notifications {data}").
            $code = ("$($lines[$i])" -replace '"(?:[^"\\]|\\.)*"', '""')
            $code = ($code -replace '//.*$', '')
            foreach ($m in [regex]::Matches($code, '\bS\.([A-Z]\w*)')) {
                $owner = $serviceOwner[$m.Groups[1].Value]
                if ($owner -and $allowed -notcontains $owner) {
                    Add-StructureViolation 'S6' "${f}:$($i + 1)" "uses service S.$($m.Groups[1].Value) of feature $owner (not in DEPENDS)"
                }
            }
            foreach ($m in [regex]::Matches($code, '(?<![\w.])([A-Z]\w*)\s*\{')) {
                $owner = $qmlOwner[$m.Groups[1].Value]
                if ($owner -and $allowed -notcontains $owner) {
                    Add-StructureViolation 'S6' "${f}:$($i + 1)" "uses widget $($m.Groups[1].Value) of feature $owner (not in DEPENDS)"
                }
            }
        }
        continue
    }

    $winOk = ($f -match '(^|/)win32/' -or $f.StartsWith('src/features/tray/hook/') -or $f.StartsWith('src/app/') -or $isTests)
    # testing/ holds fixtures that build QML engines and windows (host/testing/shell_fixtures.h).
    $qmlOk = ($f -match '(^|/)(qml|testing)/' -or $f.StartsWith('src/app/') -or $isTests)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = "$($lines[$i])"
        $at = "${f}:$($i + 1)"
        if ($line -match '^\s*#\s*include\s*<([^>]+)>') {
            $inc = $Matches[1]
            if ($inc -match $windowsHeaders -and -not $winOk) { Add-StructureViolation 'S3' $at "<$inc> outside win32/" }
            if ($inc -match $qmlIncludes -and -not $qmlOk) { Add-StructureViolation 'S4' $at "<$inc> outside qml/" }
            if ($inc -match $widgetsIncludes) { Add-StructureViolation 'S4' $at "<$inc>: Qt Widgets is not used" }
        } elseif ($line -match '^\s*#\s*include\s*"([^"]+)"') {
            $inc = $Matches[1]
            if ($inc -match '(^|/)testing/' -and -not $isTests -and $f -notmatch '(^|/)testing/') { Add-StructureViolation 'S5' $at "includes `"$inc`" outside tests" }
            # S2 (src/ only; tests/ and app/ include freely)
            if (-not $isSrc -or -not $unit -or $unit.StartsWith('app/')) { continue }
            if ($inc -notmatch '/') {
                if ($inc -notmatch '\.moc$') { Add-StructureViolation 'S2' $at "`"$inc`" is not rooted at src/" }
                continue
            }
            if ($inc.StartsWith("$unit/") -or $inc.StartsWith('kernel/')) { continue }
            if ($unit -eq 'host' -and $inc.StartsWith('control/')) { continue }
            # quickshell sits between the features and app: it registers host and feature types under
            # Quickshell's names.
            if ($unit -eq 'quickshell' -and ($inc -match '^(control|host/qml)/[^/]+$' -or
                    $inc -match '^features/[^/]+/(qml/)?[^/]+$')) { continue }
            # control/ root headers (the pipe protocol) are a stable shared abstraction: a feature
            # may use them without a DEPENDS edge.
            if ($feature -and $inc -match '^control/[^/]+$') { continue }
            # Tests of any feature may include the bar Harness and the widget helpers (host/testing/);
            # the testing/ of their DEPENDS features is allowed below.
            if (($feature -or $unit -eq 'quickshell') -and $isTests -and $inc -match '^host/testing/[^/]+$') { continue }
            $ok = $false
            if ($feature -and $inc -match '^features/([^/]+)/(.+)$') {
                $d = $Matches[1]; $rest = $Matches[2]
                if ($features[$feature].Depends -contains $d) {
                    if ($rest -notmatch '/') { $ok = $true }
                    elseif ($isTests -and $rest -match '^testing/[^/]+$') { $ok = $true }
                }
            }
            if (-not $ok) { Add-StructureViolation 'S2' $at "$unit must not include `"$inc`"" }
        } elseif ($line -match $qmlMacros -and -not $qmlOk -and $line -notmatch '^\s*//') {
            Add-StructureViolation 'S4' $at "$($Matches[0]) outside qml/"
        }
    }
}

$scope = 'files'
if ($Staged) { $scope = 'staged files' }
if ($enforced.Count -eq 0) {
    Add-Result 'structure' 'PASS' "$($checkFiles.Count) $scope, $($features.Count) features" $sw.Elapsed.TotalSeconds
} else {
    Write-Host 'Structure violations:'
    $structureRules.GetEnumerator() | ForEach-Object { Write-Host "  $($_.Key)  $($_.Value)" }
    $enforced | ForEach-Object { Write-Host "  $_" }
    Add-Result 'structure' 'FAIL' "$($enforced.Count) violation(s)" $sw.Elapsed.TotalSeconds
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
$productDirs = @('src', 'tests')
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
        Add-Result 'msvc-analyze' 'FAIL' "missing $vcvars (set WAKU_VCVARS_DIR)" 0
        Add-Result 'clang-tidy' 'SKIP' 'configure failed' 0
    } else {
        $code = Invoke-InVsEnv 'analyze-configure' $vcvars "cmake --preset $Preset -B `"$analysisDir`" -DWAKU_ANALYZE=ON -DBUILD_TESTING=OFF"
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
            $waku = Get-Command clang-tidy -ErrorAction SilentlyContinue
            $wakuExe = 'C:\Program Files\LLVM\bin\clang-tidy.exe'
            if ($waku) { $wakuExe = $waku.Source }
            if (-not (Test-Path $wakuExe)) {
                Add-Result 'clang-tidy' 'FAIL' 'clang-tidy not found on PATH or in C:\Program Files\LLVM\bin' 0
            } elseif ($code -ne 0) {
                Add-Result 'clang-tidy' 'FAIL' 'MSVC build failed; generated Qt headers may be unavailable' 0
            } else {
                $entries = Get-Content (Join-Path $analysisDir 'compile_commands.json') -Raw | ConvertFrom-Json
                # clang-cl does not accept MSVC's analyzer/anglebrackets switches. Keep the
                # original database for MSVC; /external:I still marks dependency headers.
                $wakuDir = Join-Path $analysisDir 'clang-tidy'
                New-Item -ItemType Directory -Force -Path $wakuDir | Out-Null
                foreach ($entry in $entries) {
                    $entry.command = $entry.command -replace '(?<=\s)/analyze(?=\s)', '' `
                        -replace '(?<=\s)/analyze:external-(?=\s)', '' `
                        -replace '(?<=\s)/external:anglebrackets(?=\s)', ''
                }
                $entries | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $wakuDir 'compile_commands.json')
                $units = @($entries | Where-Object {
                    $relative = [IO.Path]::GetRelativePath($repo, $_.file).Replace('\', '/')
                    $relative -match '^src/.*\.cpp$'
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
                    $commands.Add("`"$wakuExe`" -p `"$wakuDir`" --extra-arg=--target=$triple --extra-arg=-Wno-overloaded-virtual `"$unit`" >> `"$log`" 2>&1")
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
    Add-Result 'qmllint' 'SKIP' '-Fast' 0
    Add-Result 'test' 'SKIP' '-Fast' 0
} else {
    $vcvars = Get-VcvarsScript $Preset
    $buildOk = $false
    if (-not (Test-Path $vcvars)) {
        Add-Result 'configure' 'FAIL' "missing $vcvars (set WAKU_VCVARS_DIR)" 0
        Add-Result 'build' 'SKIP' 'configure failed' 0
    } else {
        Write-Step "configure (cmake --preset $Preset)"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $code = Invoke-InVsEnv 'configure' $vcvars "cmake --preset $Preset -DWAKU_ANALYZE=OFF"
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
        Add-Result 'qmllint' 'SKIP' 'build failed' 0
        Add-Result 'test' 'SKIP' 'build failed' 0
    } else {
        # qmllint over every QML module (the all_qmllint target qt_add_qml_module creates). A
        # ratchet: the step fails when the warning count rises above tools\qmllint-baseline.txt and
        # asks for the file to be lowered when it falls. The modules resolve each other through the
        # URI-shaped build copy in build\<preset>\qml_modules (src/CMakeLists.txt).
        Write-Step "qmllint (cmake --build --preset $Preset --target all_qmllint)"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $lintLog = Join-Path $repo "build\$Preset\check-qmllint.log"
        $code = Invoke-InVsEnv 'qmllint' $vcvars "cmake --build --preset $Preset --target all_qmllint > `"$lintLog`" 2>&1"
        $baselineFile = Join-Path $PSScriptRoot 'qmllint-baseline.txt'
        $baseline = [int]((Get-Content $baselineFile | Where-Object { $_ -match '^\d+$' } | Select-Object -First 1))
        $warnings = @(Select-String -Path $lintLog -Pattern '^Warning:' -ErrorAction SilentlyContinue).Count
        if ($code -ne 0) {
            Add-Result 'qmllint' 'FAIL' "exit $code; $lintLog" $sw.Elapsed.TotalSeconds
        } elseif ($warnings -gt $baseline) {
            Add-Result 'qmllint' 'FAIL' "$warnings warnings, baseline $baseline; $lintLog" $sw.Elapsed.TotalSeconds
        } elseif ($warnings -lt $baseline) {
            Add-Result 'qmllint' 'PASS' "$warnings warnings, below baseline ${baseline}: lower tools\qmllint-baseline.txt" $sw.Elapsed.TotalSeconds
        } else {
            Add-Result 'qmllint' 'PASS' "$warnings warnings (baseline)" $sw.Elapsed.TotalSeconds
        }
        Write-Step "test (ctest --test-dir build\$Preset)"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        # ctest ships with the Visual Studio CMake, so it also runs inside the vcvars environment.
        $ctestLog = Join-Path $repo "build\$Preset\check-ctest.log"
        $ctestCmd = @(
            "ctest --test-dir `"build\$Preset`" --output-on-failure --timeout 120 -j > `"$ctestLog`" 2>&1",
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
