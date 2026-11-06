#pragma once

// A copy of a real whkdrc (komorebi's sample, as one user keeps it), so the parser is tested
// against a file that exists in the wild. Tests only (rule S5).

namespace waku::keymap {

inline constexpr char kWhkdrcSample[] = R"KEYMAP(
.shell powershell

# Reload whkd configuration
alt + o                 : Get-CimInstance Win32_Process -Filter "Name = 'whkd.exe'" | ForEach-Object { Stop-Process -Id $_.ProcessId }; Start-Process whkd -ArgumentList '--config', "$env:USERPROFILE\.config\komorebi\whkdrc" -WindowStyle hidden
alt + shift + o         : komorebic replace-configuration "$env:USERPROFILE\.config\komorebi\komorebi.json"

alt + i                 : komorebic toggle-shortcuts

# App shortcuts - these require shell to be pwsh / powershell
# The apps will be focused if open, or launched if not open
# alt + f                 : if ($wshell.AppActivate('Firefox') -eq $False) { start firefox }
# alt + b                 : if ($wshell.AppActivate('Chrome') -eq $False) { start chrome }

# Open a new Windows Terminal window
alt + t                 : Start-Process wt

alt + q                 : komorebic close
alt + m                 : komorebic minimize

# Focus windows
alt + left              : komorebic focus left
alt + down              : komorebic focus down
alt + up                : komorebic focus up
alt + right             : komorebic focus right
alt + shift + oem_4     : komorebic cycle-focus previous # oem_4 is [
alt + shift + oem_6     : komorebic cycle-focus next # oem_6 is ]

# Move windows
alt + shift + left      : komorebic move left
alt + shift + down      : komorebic move down
alt + shift + up        : komorebic move up
alt + shift + right     : komorebic move right
alt + shift + return    : komorebic promote

# Stack windows
ctrl + alt + shift + left  : komorebic stack left
ctrl + alt + shift + down  : komorebic stack down
ctrl + alt + shift + up    : komorebic stack up
ctrl + alt + shift + right : komorebic stack right
alt + oem_1             : komorebic unstack # oem_1 is ;
alt + oem_4             : komorebic cycle-stack previous # oem_4 is [
alt + oem_6             : komorebic cycle-stack next # oem_6 is ]

# Resize
alt + oem_plus          : komorebic resize-axis horizontal increase
alt + oem_minus         : komorebic resize-axis horizontal decrease
alt + shift + oem_plus  : komorebic resize-axis vertical increase
alt + shift + oem_minus : komorebic resize-axis vertical decrease

# Manipulate windows
alt + f                 : komorebic toggle-float
# Float and maximize in one go; press again to tile it back (run from a tiled window)
alt + shift + f         : komorebic toggle-float; komorebic toggle-maximize
alt + shift + m         : komorebic toggle-monocle

# Window manager options
alt + shift + r         : komorebic retile
alt + p                 : komorebic toggle-pause

# Layouts
alt + x                 : komorebic flip-layout horizontal
alt + y                 : komorebic flip-layout vertical

# Workspaces
ctrl + alt + left       : komorebic cycle-workspace previous
ctrl + alt + right      : komorebic cycle-workspace next

alt + 1                 : komorebic focus-workspace 0
alt + 2                 : komorebic focus-workspace 1
alt + 3                 : komorebic focus-workspace 2
alt + 4                 : komorebic focus-workspace 3
alt + 5                 : komorebic focus-workspace 4
alt + 6                 : komorebic focus-workspace 5
alt + 7                 : komorebic focus-workspace 6
alt + 8                 : komorebic focus-workspace 7

# Move windows across workspaces
alt + shift + 1         : komorebic move-to-workspace 0
alt + shift + 2         : komorebic move-to-workspace 1
alt + shift + 3         : komorebic move-to-workspace 2
alt + shift + 4         : komorebic move-to-workspace 3
alt + shift + 5         : komorebic move-to-workspace 4
alt + shift + 6         : komorebic move-to-workspace 5
alt + shift + 7         : komorebic move-to-workspace 6
alt + shift + 8         : komorebic move-to-workspace 7
)KEYMAP";

}  // namespace waku::keymap
