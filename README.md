# LegionGoControl

LegionGoControl is a small native Windows utility for the **Lenovo Legion Go 1 / 8APU1 / Z1 Extreme**. It provides a tray-based control layer for extra Legion Go buttons, TDP presets, and the Lenovo battery charge-limit feature without using Legion Space.

This project is experimental and hardware-specific. It was built around a Legion Go 1 with Lenovo services disabled. Use it at your own risk.

## Important backend note

The current `LegionGoControl.cpp` is **not fully self-contained**.

Despite the legacy name, `LegionGoNativeWmiProbe.exe` is still required at runtime. It is no longer just a diagnostic probe: in the current architecture it acts as the native WMI backend used by the tray app for:

- setting TDP values;
- reading the battery charge-limit state;
- toggling the 80% battery charge limit.

`LegionGoControl.exe` expects the backend here:

```text
<install-folder>\LegionGoNativeWmiProbe.exe
```

For example:

```text
C:\NoInstall\LegionGoControl\LegionGoNativeWmiProbe.exe
```

Without this backend, the tray app can still load, show the tray menu, and process configurable button mappings, but TDP and battery-limit actions will fail.

## Features

### Tray application

LegionGoControl runs as a hidden-window tray application. The tray menu provides:

- Open TDP setter;
- Battery charge limit 80% toggle;
- Enable/disable logging;
- Enable/disable startup with Windows;
- Reload config;
- Open config;
- Open log;
- Exit.

A custom tray icon is generated at runtime; no external icon file is required.

### Lenovo Legion Go extra button mapper

The app registers a Raw Input HID listener for the Legion Go vendor input channel:

```text
UsagePage = 0xFFA0
Usage     = 0x0001
```

It reads the known report layout for the Legion Go extra buttons and supports these bindings:

| Button | Raw report location | Default behavior |
|---|---:|---|
| Menu | byte 18, bit `0x80` | `WIN+CTRL+O` |
| View | byte 18, bit `0x40` | `WIN+TAB` |
| Y1 | byte 20, bit `0x80` | disabled, default `F13` |
| Y2 | byte 20, bit `0x40` | disabled, default `ALT+TAB` |
| Y3 | byte 20, bit `0x20` | disabled, launch action placeholder |
| M2 | byte 20, bit `0x08` | disabled, default `F14` |
| M3 | byte 20, bit `0x04` | opens the TDP setter window |

`M1` is intentionally not mapped in this version because it overlaps with the normal gamepad/RB path on the tested device.

Supported button action types:

- `keys` — sends a hotkey through `SendInput`;
- `launch` — starts an external executable or script;
- `internal` — currently used for `show_tdp`.

Supported hotkey tokens include common modifiers and keys such as:

```text
CTRL, SHIFT, ALT, WIN, ENTER, ESC, TAB, SPACE, BACKSPACE,
DELETE, INSERT, HOME, END, PGUP, PGDN, UP, DOWN, LEFT, RIGHT,
A-Z, 0-9, F1-F24
```

Example hotkeys:

```text
WIN+TAB
ALT+TAB
CTRL+SHIFT+ESC
F13
```

### TDP setter

The app exposes a small TDP window with these presets:

```text
5W, 8W, 10W, 12W, 16W, 20W, 25W, 30W, Custom
```

Preset buttons set the same wattage to all three Lenovo WMI TDP limits:

```text
STAPM / FAST / SLOW
```

The runtime backend currently uses the following Lenovo WMI feature IDs:

```text
STAPM = 0x0101FF00
FAST  = 0x0102FF00
SLOW  = 0x0103FF00
```

Safety validation in the tray app enforces:

```text
minimum: 5W
maximum: 35W
STAPM <= FAST
STAPM <= SLOW
```

The app does **not** continuously read or display current TDP. TDP is written only when the user explicitly presses a preset/custom button.

### Custom TDP

The custom TDP button reads values from the INI file:

```ini
[TDP]
CustomStapmW=12
CustomFastW=16
CustomSlowW=16
```

After editing these values, use **Reload config** from the tray menu.

### Battery charge limit

The tray menu can toggle Lenovo's battery charge-limit feature. The backend uses:

```text
Battery charge limit 80% = 0x03010001
```

Observed values:

```text
0 = battery charge limit disabled
1 = battery charge limit enabled
```

When enabled, Lenovo firmware may stop charging around the 75-80% range. This is normal hysteresis behavior for this firmware feature and does not necessarily mean the battery is capped at exactly 80.0%.

The current tray item is a toggle. If the item is checked, the backend state file reports the feature as enabled. If you need to verify the real firmware state, query `0x03010001` through the backend or through WMI.

### Startup and elevation

TDP and battery-limit operations require elevated access. The app is designed to run elevated through a Windows Scheduled Task named:

```text
LegionGoControl
```

If `LegionGoControl.exe` is launched without administrator rights, it attempts to run the scheduled task and then exits.

If another instance is already running, launching the app again signals the existing instance and opens the TDP window.

## What this project does not do

- It does not require Legion Space.
- It does not require RyzenAdj.
- It does not require PowerShell for normal runtime operation.
- It does not continuously poll TDP.
- It does not display live TDP telemetry.
- It does not implement M1 mapping because of the RB/gamepad overlap on the tested Legion Go.
- It is not a generic handheld-control utility. The Raw Input mapping is specific to the tested Legion Go report layout.

## Requirements

- Windows 10/11 x64.
- Lenovo Legion Go 1 / Z1 Extreme / 8APU1.
- Microsoft Visual Studio Build Tools or Visual Studio with the MSVC C++ toolchain.
- Administrator rights.
- `LegionGoNativeWmiProbe.exe` placed next to `LegionGoControl.exe`.

Recommended install directory:

```text
C:\NoInstall\LegionGoControl
```

## Build

Open an **x64 Native Tools Command Prompt for VS** in the source directory.

Compile `LegionGoControl.cpp`:

```bat
cl /nologo /std:c++17 /EHsc /W4 /O2 /DUNICODE /D_UNICODE LegionGoControl.cpp user32.lib shell32.lib gdi32.lib advapi32.lib /link /SUBSYSTEM:WINDOWS /OUT:LegionGoControl.exe
```

If you also have the backend source, compile it as a console executable:

```bat
cl /nologo /std:c++17 /EHsc /W4 /O2 /DUNICODE /D_UNICODE LegionGoNativeWmiProbe.cpp advapi32.lib ole32.lib oleaut32.lib wbemuuid.lib /link /SUBSYSTEM:CONSOLE /OUT:LegionGoNativeWmiProbe.exe
```

The final install folder must contain at least:

```text
LegionGoControl.exe
LegionGoNativeWmiProbe.exe
```

The INI and log files are created at runtime in the same folder.

## Install

From an elevated PowerShell window:

```powershell
$InstallDir = "C:\NoInstall\LegionGoControl"
$TaskName = "LegionGoControl"

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

Copy-Item -Force ".\LegionGoControl.exe" "$InstallDir\LegionGoControl.exe"
Copy-Item -Force ".\LegionGoNativeWmiProbe.exe" "$InstallDir\LegionGoNativeWmiProbe.exe"

Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue

$Action = New-ScheduledTaskAction `
    -Execute "$InstallDir\LegionGoControl.exe" `
    -WorkingDirectory $InstallDir

$Trigger = New-ScheduledTaskTrigger -AtLogOn
$Trigger.Delay = "PT20S"

$Principal = New-ScheduledTaskPrincipal `
    -UserId "$env:USERNAME" `
    -LogonType Interactive `
    -RunLevel Highest

$Settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit (New-TimeSpan -Seconds 0)

Register-ScheduledTask `
    -TaskName $TaskName `
    -Action $Action `
    -Trigger $Trigger `
    -Principal $Principal `
    -Settings $Settings `
    -Description "Starts LegionGoControl elevated at user logon."

Start-ScheduledTask -TaskName $TaskName
```

## Manual start

```powershell
Start-ScheduledTask -TaskName "LegionGoControl"
```

or:

```bat
schtasks /Run /TN "LegionGoControl"
```

## Default configuration

On first run, the app creates:

```text
LegionGoControl.ini
```

Default config:

```ini
[General]
logging=0
debounce_ms=40
action_cooldown_ms=250

[Menu]
enabled=1
trigger=down
action=keys
keys=WIN+CTRL+O

[View]
enabled=1
trigger=down
action=keys
keys=WIN+TAB

[Y1]
enabled=0
trigger=down
action=keys
keys=F13

[Y2]
enabled=0
trigger=down
action=keys
keys=ALT+TAB

[Y3]
enabled=0
trigger=down
action=launch
path=
args=
working_dir=

[M2]
enabled=0
trigger=down
action=keys
keys=F14

[M3]
enabled=1
trigger=down
action=internal
internal=show_tdp

[TDP]
CustomStapmW=12
CustomFastW=16
CustomSlowW=16
```

## Config reference

### General

```ini
[General]
logging=0
debounce_ms=40
action_cooldown_ms=250
```

- `logging`: `0` or `1`.
- `debounce_ms`: button state debounce window.
- `action_cooldown_ms`: minimum cooldown between repeated actions.

### Button section

Each button can have its own section:

```ini
[ButtonName]
enabled=1
trigger=down
action=keys
keys=WIN+TAB
```

Supported button names:

```text
Menu, View, Y1, Y2, Y3, M2, M3
```

Supported triggers:

```text
down
up
```

Supported actions:

```text
keys
launch
internal
none
```

### Keys action

```ini
action=keys
keys=CTRL+SHIFT+ESC
```

### Launch action

```ini
action=launch
path=C:\Path\To\Program.exe
args=optional arguments
working_dir=C:\Path\To
```

### Internal action

```ini
action=internal
internal=show_tdp
```

Accepted aliases in the current code include:

```text
show_tdp
show_tdp_window
tdp
show_menu
```

They all open the TDP setter window in this version.

## Logs and state files

Runtime files are stored in the install directory:

```text
LegionGoControl.ini
LegionGoControl.log
LegionGoBatteryLimitState.txt
LegionGoNativeWmiProbe_state.txt
LegionGoNativeWmiProbe.log
```

Notes:

- `LegionGoControl.log` is written by the tray app when logging is enabled or for important events.
- `LegionGoBatteryLimitState.txt` is written by the backend and read by the tray menu to show the battery-limit checkmark.
- `LegionGoNativeWmiProbe_state.txt` is related to backend TDP operations. The current tray app deletes it around explicit TDP set operations because it does not display live TDP status.
- `LegionGoNativeWmiProbe.log` is used by the backend for errors and diagnostics.

## Troubleshooting

### TDP or battery actions fail

Check that the backend exists in the same folder:

```text
C:\NoInstall\LegionGoControl\LegionGoNativeWmiProbe.exe
```

Then check:

```text
LegionGoNativeWmiProbe.log
LegionGoControl.log
```

The tray app must run elevated. The recommended way is through the scheduled task.

### The battery charge limit remains enabled

The firmware state is controlled by Lenovo WMI feature `0x03010001`.

Expected values:

```text
0 = disabled
1 = enabled
```

If charging stops around 75-80%, verify that the feature value is `0`. When the feature is enabled, Lenovo firmware may hold the battery in a 75-80% band.

### Charging slows down above 85-90%

This can be normal battery charge tapering. The battery may charge quickly at lower percentages and then reduce current near full charge. A low charge rate near 90-100% does not necessarily indicate a fault.

### Extra buttons do not trigger

Check that:

- the app is running;
- only one instance is running;
- the INI button section is enabled;
- the expected button is one of `Menu`, `View`, `Y1`, `Y2`, `Y3`, `M2`, `M3`;
- the Lenovo HID report layout matches the tested Legion Go 1 layout.

## Uninstall

From an elevated PowerShell window:

```powershell
Stop-Process -Name "LegionGoControl" -Force -ErrorAction SilentlyContinue
Unregister-ScheduledTask -TaskName "LegionGoControl" -Confirm:$false -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "C:\NoInstall\LegionGoControl"
```

## Safety notes

Changing TDP and battery firmware options can affect thermals, stability, battery behavior, and fan behavior. The app only exposes the Lenovo WMI IDs listed above, but the behavior is still controlled by Lenovo firmware.

Do not use this on unsupported hardware unless you have verified the WMI feature IDs and Raw Input report layout yourself.
