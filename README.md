# LegionGoControl V2.1 (beta)

LegionGoControl is a native Windows tray utility for the **Lenovo Legion Go 1 / 8APU1 / Z1 Extreme**. V2.1 adds a graphical settings interface, per-executable TDP profiles, named firmware fan-curve profiles and an autonomous performance overlay while keeping the original Raw Input button mapper and Lenovo WMI backend.

> **Hardware beta:** fan telemetry, curve read/write/read-back and restore have been tested remotely on model `83E1 / Legion Go 8APU1`, BIOS `N3CN37WW`. Lenovo HID, TDP and battery behavior still require target-device validation before daily use, and other BIOS versions remain unverified.

## Components

```text
LegionGoControl.exe
├─ tray and Settings UI
├─ Legion Go Raw Input mapper
├─ process monitor and Game Profiles resolver
├─ click-through performance overlay and native metric collectors
├─ verified AMD Radeon Chill frame limiter with crash recovery
└─ serialized calls to the Lenovo backend

LegionGoNativeWmiProbe.exe
├─ Lenovo WMI access through ROOT\WMI / LENOVO_OTHER_METHOD / LENOVO_FAN_METHOD
├─ verified TDP transactions with rollback attempt
├─ verified fan curve transactions, telemetry, backup and restore
└─ verified battery-limit status/set/toggle
```

Both executables must remain in the same directory. FPS/frame timing is collected directly by LegionGoControl through Windows ETW; no RTSS or separate collector is required.

## Tray menu

The V2.1 tray is intentionally small:

- **Settings...** — opens General, Controller, TDP, Fan, Overlay and Info settings;
- **Game Profiles...** — manages per-executable TDP profiles;
- version header (`LegionGoControl v2.1 YYYYMMDD #abcde`) and active target status;
- **Exit**.

Startup, battery limit and logging were moved from the tray into **Settings > General**.

## Settings

### General

- Windows screen brightness slider (`0-100%`) through the standard `WmiMonitorBrightness` interface;
- diagnostic logging;
- HID debounce (`0-1000 ms`);
- action cooldown (`50-5000 ms`);
- elevated “Start with Windows” scheduled task;
- Lenovo battery charge limit 80%, with firmware read-back;
- open application log.

### Controller

The Controller tab provides a compact editor for:

```text
Menu, View, Y1, Y2, Y3, M2, M3
```

For each button:

- enabled/disabled;
- trigger on button down/up;
- action `none`, `keys`, `launch` or `internal`;
- simulated key combination;
- executable path, arguments and working directory;
- internal action.

M1 remains unsupported because it overlaps the normal RB/gamepad path on the tested V1 device.

### Info

The Info tab contains the V2.1 version generated at build time from the commit date and its last five hexadecimal characters (`YYYYMMDD #abcde`), a short English description, the GitHub repository link, hardware-beta notice, and the required attribution/link for the application icon.

### TDP

Every numeric field has a Windows up/down control. The displayed and keyboard-selection order is:

```text
STAPM / SLOW / FAST
```

Allowed range is `5-35 W`, with:

```text
STAPM <= SLOW <= FAST
```

This is the conventional AMD hierarchy documented by the RyzenAdj option guide: STAPM is the sustained limit, SLOW is the average/medium-duration limit, and FAST is the short boost ceiling. Equality remains valid for balanced presets. Raising STAPM automatically raises SLOW and FAST when necessary; raising SLOW automatically raises FAST. Because firmware may still clamp or rewrite a value, the backend performs full read-back verification.

Reference: <https://github.com/FlyGoat/RyzenAdj/wiki/Options#stapm>

Balanced preset buttons set all three values to the same wattage. The base target is persisted and is restored whenever no configured Game Profile is running.

### Fan

The Fan tab provides a DPI-aware graph with ten draggable firmware nodes. Temperatures are fixed by the Legion Go firmware at `10, 20, ... 100 °C`; each node controls duty percent and shows an RPM estimate based on hardware calibration. The tab reads actual CPU temperature and fan RPM once per second while open, and displays the interpolated curve command percentage.

Named curves are managed from the profile combo above the graph. **New** copies the current curve into a new draft, **Save** immediately stores the current name and curve, and **Delete** removes the selected draft when Settings is applied. Selecting another profile loads its ten points. Apply/OK also persists all profile changes and records the selected profile.

Curve writes are explicit, validated, read back exactly and rolled back on failure. The backend preserves the original firmware curve on the first apply and restores it when custom control is disabled or LegionGoControl exits cleanly. Safety floors prevent decreasing curves and require at least `60% @ 80 °C`, `80% @ 90 °C`, and `85% @ 100 °C`. Firmware emergency protection always retains priority.

Hardware observations on BIOS `N3CN37WW`: `28%` stabilized near `2100 RPM`; `84%` stabilized near `6280 RPM`; firmware ramps speed changes gradually rather than stepping immediately.

### Overlay

The Overlay tab configures an autonomous RTSS-style display. It is a normal topmost, no-activate, click-through Windows window: LegionGoControl does not inject DLLs or code into games and does not depend on RTSS. A configurable global `F1-F24` key toggles it. Scale (`50-200%`), black overlay opacity (`0-100%`, background and text fade together), screen corner and X/Y margins are configurable. Font size follows Windows DPI rather than the game's render resolution, so switching to 1280x800 fullscreen does not shrink it.

Two layouts are available: the original vertical panel and a compact full-width top performance bar. The top bar follows the fixed colored labels `FPS`, `Z1E`, `780M`, `RAM`, `FAN`, `BATT`, followed by the 24-hour clock.

Metrics update once per second and are rendered in this fixed order:

1. FPS;
2. CPU usage;
3. CPU temperature;
4. CPU package power;
5. GPU usage;
6. VRAM used / configured total;
7. occupied RAM / total RAM;
8. fan RPM;
9. remaining battery percentage;
10. local time in 24-hour format.

When **Capture FPS (continuous ETW)** is enabled, FPS uses the original native collector from the first FPS release: one real-time `Microsoft-Windows-DXGI` / `Microsoft-Windows-D3D9` ETW session remains active for the application lifetime. Desktop/foreground transitions and F10 visibility toggles never stop or restart it; hiding the overlay preserves the rolling FPS samples. Frame streams are grouped by PID: the foreground presenter is preferred, with an automatic dominant-stream fallback for launchers and games whose rendering process owns no foreground window. This intentionally restores the earlier behavior, including the possibility of seeing a dominant desktop graphics stream instead of suppressing FPS on Windows. The low-impact default avoids PDH entirely: GPU utilization, VRAM used and CPU package power show `N/A`, while DXGI supplies only VRAM capacity/budget. **Detailed GPU/power counters** can opt back into Windows GPU Engine and AMD `Energy Meter (RAPL_Package0_PKG)\\Power` PDH polling, but may affect sensitive games. Memory, battery and time use Win32; CPU temperature and fan RPM use the verified Lenovo backend. Unavailable values are shown as `N/A` without hiding other rows.

Because the overlay deliberately avoids game injection, a protected game or true exclusive-fullscreen presentation may cover it. Borderless/windowed fullscreen is the most compatible mode.

### FPS limiter

Settings > General provides a base FPS limiter from `30` to `144 FPS`. Each Game Profile can optionally override that base target with its own `30-144 FPS` slider. The limiter uses the AMD driver-provided Radeon Chill interface in `amdadlx64.dll`, setting Chill minimum and maximum to the same target; it does not use RTSS, process suspension or game injection.

Before the first change LegionGoControl stores the previous Radeon Chill enabled state and both minimum/maximum FPS values. Every change is read back through the driver. The original Radeon state is restored when limiting is disabled and on clean exit, while a persistent backup enables recovery after a crash or forced termination. On the tested Radeon 780M driver Chill reports a native `30-300 FPS` range; LegionGoControl intentionally exposes only `30-144 FPS`.

## Game Profiles

Use **Game Profiles...** and **+ Add profile** to configure:

- profile name;
- exact executable path selected through the Windows file picker;
- STAPM, SLOW and FAST target values.

Profiles can be edited, removed and reordered. Matching uses the normalized, case-insensitive **full executable path**. An executable with the same filename in a different directory does not match.

### Runtime behavior

The process monitor scans approximately every 750 ms using `CreateToolhelp32Snapshot`, PID/parent PID, process creation time and `QueryFullProcessImageNameW`.

1. A process already open when LegionGoControl starts is detected.
2. Starting a configured executable applies its profile once.
3. Child processes automatically inherit the profile. A chain such as `RetroBat -> EmulationStation -> emulator` remains active even after the launcher exits.
4. Process identity combines PID and creation time to protect against PID reuse. A recently exited parent is retained briefly so its child can still be attached on the next scan.
5. Multiple instances and descendants keep the profile active until the final member of the process family exits.
6. If multiple profile families run, the current profile remains active while its family exists; otherwise the first matching profile in configured order wins.
7. Switching profiles applies the new target directly, without restoring base in between.
8. Closing the final profiled process family restores the persisted base/manual TDP.
9. Changing base TDP while a profile is active changes the future restore target; it does not replace the active game target.

Automatic following requires LegionGoControl to observe the configured root executable at least once. No Windows API can reconstruct a launcher relationship that ended before LegionGoControl itself started.

The active profile and latest backend result are shown in the tray tooltip, tray menu and Game Profiles list.

## V1 INI compatibility

Configuration remains in:

```text
LegionGoControl.ini
```

V1 General and per-button sections continue to load. Legacy `[Buttons] Name=HOTKEY` values are also accepted.

V2 stores the base target in:

```ini
[TDP]
BaseStapmW=16
BaseFastW=20
BaseSlowW=20
```

If those keys do not exist, V2 imports its initial base from the V1 `CustomStapmW`, `CustomFastW` and `CustomSlowW` values.

Game Profile order and stable GUID sections use:

```ini
[GameProfiles]
Order={guid-1},{guid-2}

[GameProfile.{guid-1}]
Name=EA FC 26
Path=C:\Games\EA FC 26\FC26.exe
StapmW=16
FastW=16
SlowW=16
FpsLimitEnabled=1
FpsLimit=60
```

Fan preferences use:

```ini
[Fan]
Enabled=0
SelectedProfile={fan-guid-1}
Duty0=44
Duty1=48
; ... Duty9, corresponding to 10..100 C

[FanProfiles]
Order={fan-guid-1},{fan-guid-2}

[FanProfile.{fan-guid-1}]
Name=Balanced
Duty0=44
; ... Duty9

[Overlay]
VisibleAtStartup=0
FunctionKey=10
ScalePercent=100
OpacityPercent=85
Corner=1
MarginX=20
MarginY=20
Style=1

[General]
FpsLimitEnabled=0
FpsLimit=60
```

Unknown INI keys and sections are not intentionally rewritten or deleted.

## Lenovo backend

The hardware-specific identifiers remain:

```text
STAPM = 0x0101FF00
FAST  = 0x0102FF00
SLOW  = 0x0103FF00
Battery limit 80% = 0x03010001
Fan full-speed override = 0x04020000
Fan RPM telemetry = 0x04030001
CPU temperature = 0x05040000
```

The backend connects to:

```text
ROOT\WMI
LENOVO_OTHER_METHOD
LENOVO_OTHER_METHOD.InstanceName="ACPI\\PNP0C14\\GMZN_0"
LENOVO_FAN_METHOD / Fan_Get_Table / Fan_Set_Table
```

V2 improvements:

- strict CLI integer parsing;
- COM objects released through RAII;
- persistent append-only diagnostics instead of deleting the previous log;
- atomic state-file replacement;
- serialized TDP and battery transactions;
- ordered TDP writes;
- read-back verification after writes;
- best-effort rollback to the pre-transaction TDP triple;
- battery values other than `0` and `1` treated as unknown/error.

A timeout or failed verification means the hardware state may be indeterminate. Check the backend log before retrying.

## Runtime files

Stored beside the executables:

```text
LegionGoControl.ini
LegionGoControl.log
LegionGoNativeWmiProbe.log
LegionGoNativeWmiProbe_state.txt
LegionGoBatteryLimitState.txt
LegionGoFanState.txt
LegionGoFanBackup.txt
LegionGoFpsBackup.ini
```

For V2.1 beta testing, use a directory writable only by the intended administrator account. The frontend currently still runs elevated, so controller `launch` actions inherit elevated privileges.

## Build

Requirements:

- Windows 10/11 x64;
- Visual Studio 2022/2026 or Build Tools with MSVC C++ x64 workload;
- Windows SDK.

Run:

```bat
build.bat
```

Outputs:

```text
LegionGoControl.exe
LegionGoNativeWmiProbe.exe
build\LegionGoCoreTests.exe
```

The build uses C++17, UTF-8 source encoding, `/W4`, `/O2`, x64 Release, embeds the application icon and includes the Per-Monitor DPI-aware manifest. `build.bat` generates `build/LegionGoBuildVersion.h` from the checked-out Git commit date and five-character hash suffix; Info, the tray menu and the tray tooltip therefore identify the source revision automatically. Icon attribution is documented in `assets/README.md` and displayed in Settings > Info.

## Non-hardware tests

```bat
test.bat
```

The pure-core suite tests:

- TDP range and relationship validation;
- Windows path normalization and exact matching;
- profile validation and duplicate detection;
- deterministic arbitration with multiple running executables;
- automatic launcher/child/grandchild process-family tracking and PID reuse;
- base/profile target and restore semantics.

The tests never invoke WMI, change TDP, toggle battery state or launch the tray application.

## Install / startup

Place both executables in a protected directory, for example:

```text
C:\NoInstall\LegionGoControl
```

Start the application once as administrator, then enable **Start with Windows** under Settings > General. The setting creates a scheduled task named `LegionGoControl` with highest privileges.

If the application is started without elevation, it attempts to run that scheduled task and exits.

## Legion Go validation checklist

Before daily use, validate on the actual device:

1. Settings and tray render correctly at the device DPI.
2. Menu, View, Y1/Y2/Y3 and M2/M3 generate only the intended actions.
3. Each manual preset is read back correctly by the backend.
4. Battery state and toggle agree with Lenovo firmware behavior.
5. A harmless test executable activates one low-power profile.
6. A second executable exercises deterministic profile switching.
7. Closing the final executable restores the exact base triple.
8. Suspend/resume, app restart and forced game termination do not leave an unwanted target active.
9. Backend timeout/failure is shown as an error and does not claim success.
10. Fan profiles survive restart and selecting each profile restores its ten saved points.
11. The overlay hotkey toggles without focusing it; values refresh once per second and clicks pass through to the game.
12. Compare FPS with another trusted counter and CPU/GPU/RAM values with Windows Task Manager during a controlled workload.
13. Enable a harmless 60 FPS base limit, verify AMD Radeon Chill min/max read-back, disable it, and confirm the previous Radeon state is restored.
14. Configure a different Game Profile FPS target and verify base/profile switching and final restore.

## Safety

Changing firmware power limits can affect temperatures, stability, fan behavior and battery life. This project is specific to the tested Legion Go generation. Do not run the backend on unsupported hardware unless the WMI instance and feature IDs have been independently verified.
