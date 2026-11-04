<#
.SYNOPSIS
    Static audit of a waku config home for stubs, mock data and Linux-only calls.
.DESCRIPTION
    Scans every .qml and .js file under -ConfigHome. A match is a finding to fix or an element to
    drop. Exit code 0 means no pattern matched. That does not prove the shell works: run
    run-candidate.ps1 and exercise the elements (reference/verify.md).
.PARAMETER ConfigHome
    The config home to scan.
.PARAMETER Allow
    Rule ids to skip, for a match you checked by hand and can explain to the user.
.EXAMPLE
    pwsh -NoProfile -File audit.ps1 -ConfigHome D:\shells\mine
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ConfigHome,
    [string[]]$Allow = @()
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $ConfigHome -PathType Container)) { throw "No such folder: $ConfigHome" }

# id, regex (per line unless Multiline), why
$rules = @(
    @{ Id = 'empty-handler'; Rx = 'on(Clicked|Triggered|Pressed|Released|Toggled|Accepted|Activated)\s*:\s*(\{\s*\}|\(\s*\)\s*=>\s*\{\s*\})'; Why = 'handler does nothing' },
    @{ Id = 'comment-only-handler'; Rx = 'on(Clicked|Triggered|Pressed|Toggled)\s*:\s*\{\s*//'; Why = 'handler holds only a comment' },
    @{ Id = 'placeholder-word'; Rx = '(?i)\b(TODO|FIXME|XXX|mock|stub|dummy|fake|placeholder|lorem ipsum|not implemented)\b'; Why = 'placeholder or mock marker' },
    @{ Id = 'hardcoded-live-value'; Rx = '(?i)\b(text|value|percentage|label)\s*:\s*"?\d{1,3}\s*(%|°|(C|MB|GB|kB|W|h)\b)'; Why = 'a number shown as if it were live' },
    @{ Id = 'hardcoded-name'; Rx = '(?i)\b(text|label)\s*:\s*"[^"]*\b(Connected|Wi-?Fi|Ethernet|Playing|Charging|AirPods|Headphones)\b[^"]*"\s*$'; Why = 'status text that should come from a service' },
    @{ Id = 'static-model'; Rx = 'model\s*:\s*\[\s*("[^"]*"\s*,?\s*){1,}\]|model\s*:\s*[1-9]\d?\s*$'; Why = 'a list or count that stands in for real data' },
    @{ Id = 'disabled-placeholder'; Rx = '\b(visible|enabled)\s*:\s*false\b'; Why = 'an element switched off permanently; drop it if it never turns on' },
    @{ Id = 'hyprland'; Rx = '\bHyprland(FocusGrab|Monitor|Workspace|Toplevel|Event)?\b|hyprctl|niri\s+msg|swaymsg'; Why = 'Hyprland/Niri/Sway has no data on Windows' },
    @{ Id = 'tray-menu'; Rx = '\bQsMenuOpener\b|\bQsMenuHandle\b'; Why = 'tray menus hold no entries on waku' },
    @{ Id = 'wayland-only'; Rx = '\b(ScreencopyView|WlSessionLock|SessionLock|Greetd|Polkit)\b'; Why = 'not provided on Windows' },
    @{ Id = 'linux-command'; Rx = '"(bash|sh|zsh|fish|nmcli|iwctl|pactl|wpctl|amixer|playerctl|brightnessctl|systemctl|loginctl|journalctl|notify-send|grim|slurp|wl-copy|wl-paste|xdg-open|awk|sed|jq|upower|bluetoothctl|dbus-send|busctl|swww|hyprpaper|matugen|wallust)"|"/(usr|etc|home|run|tmp|var)/' ; Why = 'a Linux command or path that does not exist on Windows' },
    @{ Id = 'home-path'; Rx = '"~/|\$HOME|XDG_(CONFIG|CACHE|DATA|RUNTIME)'; Why = 'Linux path or variable; use the config home or %LOCALAPPDATA%' },
    @{ Id = 'int-workspace-id'; Rx = 'property\s+int\s+\w*([wW]orkspace|ws)\w*[iI]d\b|signal\s+\w+\(\s*int\s+\w*[wW]orkspace'; Why = 'workspace ids are strings such as DISPLAY1:0' },
    @{ Id = 'null-gated-visible'; Rx = 'visible\s*:\s*[\w.]*(adapter|device|player|service)\w*\s*(!==|!=)\s*null'; Why = 'a service is null until ready; the panel may never show' }
)

$files = @(Get-ChildItem -LiteralPath $ConfigHome -Recurse -File -Include *.qml, *.js, *.mjs |
        Where-Object { $_.FullName -notmatch '[\\/](\.git|node_modules|smoke|shots|logs|state)[\\/]' })
if ($files.Count -eq 0) { Write-Host "No .qml or .js files under $ConfigHome"; exit 1 }

$findings = [System.Collections.Generic.List[object]]::new()
foreach ($f in $files) {
    $lines = Get-Content -LiteralPath $f.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($r in $rules) {
            if ($Allow -contains $r.Id) { continue }
            if ($lines[$i] -match $r.Rx) {
                $rel = [System.IO.Path]::GetRelativePath($ConfigHome, $f.FullName)
                $findings.Add([pscustomobject]@{
                        Rule = $r.Id
                        Where = "${rel}:$($i + 1)"
                        Why = $r.Why
                        Text = $lines[$i].Trim()
                    })
            }
        }
    }
}

# A shell.qml is required.
if (-not (Test-Path -LiteralPath (Join-Path $ConfigHome 'shell.qml'))) {
    $findings.Add([pscustomobject]@{ Rule = 'no-shell'; Where = 'shell.qml'; Why = 'the config home has no shell.qml'; Text = '' })
}

if ($findings.Count -eq 0) {
    Write-Host "audit: no pattern matched in $($files.Count) files. This is not proof the shell works."
    exit 0
}
$findings | Format-Table Rule, Where, Why, Text -AutoSize -Wrap | Out-String -Width 200 | Write-Host
Write-Host "audit: $($findings.Count) findings in $($files.Count) files. Fix each one or drop the element."
exit 1
