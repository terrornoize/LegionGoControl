# LegionGoControl V2 (beta)

LegionGoControl is a native Windows tray utility for the **Lenovo Legion Go 1 / 8APU1 / Z1 Extreme**. V2 adds a graphical settings interface and per-executable TDP profiles while keeping the original Raw Input button mapper and Lenovo WMI backend.

> **Hardware beta:** the application and pure logic have been built and tested on a normal Windows PC, not on a Legion Go. Lenovo HID input, firmware TDP writes, battery charge limit and restore behavior must be validated on the target Legion Go before daily use.

## Components

```text
LegionGoControl.exe
├─ tray and Settings UI
├─ Legion Go Raw Input mapper
├─ process monitor and Game Profiles resolver
└─ serialized calls to the backend

LegionGoNativeWmiProbe.exe
├─ Lenovo WMI access through ROOT\WMI / LENOVO_OTHER_METHOD
├─ verified TDP transactions with rollback attempt
└─ verified battery-limit status/set/toggle
```

Both executables must remain in the same directory.

## Tray menu

The V2 tray is intentionally small:

- **Open TDP setter** — opens the TDP tab in Settings;
- **Settings...** — opens General, Controller, TDP and Info settings;
- **Game Profiles...** — manages per-executable TDP profiles;
- version header (`LegionGoControl v2 YYYYMMDD`) and active target status;
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

The Info tab contains the current dated V2 version, a short English description, the GitHub repository link, hardware-beta notice, and the required attribution/link for the application icon.

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
```

Unknown INI keys and sections are not intentionally rewritten or deleted.

## Lenovo backend

The hardware-specific identifiers remain:

```text
STAPM = 0x0101FF00
FAST  = 0x0102FF00
SLOW  = 0x0103FF00
Battery limit 80% = 0x03010001
```

The backend connects to:

```text
ROOT\WMI
LENOVO_OTHER_METHOD
LENOVO_OTHER_METHOD.InstanceName="ACPI\\PNP0C14\\GMZN_0"
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
```

For V2 beta testing, use a directory writable only by the intended administrator account. The frontend currently still runs elevated, so controller `launch` actions inherit elevated privileges.

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

The build uses C++17, UTF-8 source encoding, `/W4`, `/O2`, x64 Release, embeds the application icon and includes the Per-Monitor DPI-aware manifest. Icon attribution is documented in `assets/README.md` and displayed in Settings > Info.

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

## Safety

Changing firmware power limits can affect temperatures, stability, fan behavior and battery life. This project is specific to the tested Legion Go generation. Do not run the backend on unsupported hardware unless the WMI instance and feature IDs have been independently verified.
