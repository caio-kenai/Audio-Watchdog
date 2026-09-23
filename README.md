# Audio Watchdog

Monitors Windows audio endpoints and **prevents applications from using
exclusive audio mode**. Any device that is switched back to exclusive mode —
by an app, a driver update, the user, or Windows itself — is forced back to
shared mode within seconds, and kept that way.

It is a native application (C++20 / Win32 / Core Audio) that normally runs
**in the system notification area (system tray)** with a small icon, watching
continuously and writing logs. It can also be installed as a Windows service if
machine-wide enforcement with no logged-on user is required. Either way it is
transparent for everything else: normal audio apps keep using WASAPI shared
mode exactly as before.

---

## Why

Windows lets applications take **exclusive control** of a sound device. When
that happens, every other app loses access to the device. For producers using
ASIO, that can mean shared-mode applications (browser, Zoom, OBS) suddenly
silent, or exclusive apps grabbing the endpoint and stealing it from the DAW.

The opposite of the usual approach (allow and hope) is used here: the setting
**"Allow applications to take exclusive control of this device"** is a target
that this service *enforces off*:

```
DETECT ──▶ if "exclusive control" is enabled ──▶ DISABLE it ──▶ VERIFY it stuck
                                                    │
                 notified on any device change ◀───┘    └─▶ re-check every N s (fallback)
```

Because Windows and audio drivers can silently restore this setting on
updates, the service both listens to real-time MMDevice notifications **and**
re-checks periodically, so the state is re-corrected no matter how it changed.

---

## Requirements

- Windows 10 / Windows 11 (x64)
- Visual Studio 2022 (Build Tools) — MSVC 14.4x, C++20
- [CMake](https://cmake.org/) ≥ 3.20 and [Ninja](https://ninja-build.org/)
  (a `cmake`/`ninja` bundle ships inside VS Build Tools)

The project has **zero external dependencies** (no WIL, no vcpkg).

---

## Building

```bat
set VSCMD="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call %VSCMD%
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Outputs: `build\bin\AudioWatchdog.exe` (tray app + service + CLI), `build\bin\AudioWatchdogSetup.exe` (self-contained installer, copied to `installer\`) and `build\bin\audiowatchdog_tests.exe` (unit tests).

`scripts\build.ps1` wraps the same steps and runs the test suite afterwards.

> The first time you run the freshly built unsigned binary, Windows Defender's
> machine-learning heuristic may flag it (`Trojan:Win64/Doina.ND!MTB` is a
> well-known false positive on unsigned native binaries that touch the SCM).
> Add an exclusion for your build folder, or sign the binary.

---

## Usage

```
AudioWatchdog                       run in the system tray (default)
AudioWatchdog <command> [options]
```

| Command | Description |
|---|---|
| *(none)* | Run in the notification area: icon, context menu, live monitoring |
| `install` | Install the service (automatic start, restart-on-failure) |
| `uninstall` | Remove the service |
| `start` / `stop` / `restart` | Service lifecycle |
| `status` | Show state, PID, start type |
| `scan` | Run one enforcement pass immediately |
| `devices` | List every endpoint and its exclusive-mode state |
| `diagnose` | Self-check: OS, endpoints, store read/write, WASAPI probe |
| `diagnose --probe-exclusive` | Also open a real exclusive WASAPI stream to prove the state |
| `version` | Version info |
| `help` | This list |

`install`, `uninstall`, `start`, `stop`, and `restart` require elevation; the
tool re-launches itself elevated when needed.

### System tray mode

Running without arguments starts the tray resident: it enforces the policy in
the background and offers a context menu with **Scan now**, **Open log file**,
**Open config file**, **Start when I log in**, and **Exit**. "Start when I log
in" is enabled automatically on first run and can be toggled from the menu
(HKCU `Run` value `Audio Watchdog`). A second instance is refused.

### Installer

`installer\AudioWatchdogSetup.exe` (produced by the build) is a self-contained
setup that installs to `Program Files\Audio Watchdog`, creates the
`ProgramData\Audio Watchdog` folders with proper permissions, creates a Start
Menu shortcut, sets up logon autostart, and launches the tray app. Requires
elevation (UAC). `installer\install.ps1` / `uninstall.ps1` do the same
manually.

Typical flow:

```bat
AudioWatchdog install
AudioWatchdog start
AudioWatchdog status
AudioWatchdog devices
AudioWatchdog scan
AudioWatchdog diagnose --probe-exclusive
```

---

## Configuration

`C:\ProgramData\Audio Watchdog\config.ini` (created on first run):

```ini
[Monitor]
Playback=true              ; watch render endpoints
Capture=true               ; watch capture endpoints
CheckIntervalSeconds=60    ; periodic re-check fallback (1..86400)

[Logging]
Enable=true                ; write logs
Level=INFO                 ; DEBUG | INFO | WARN | ERROR

[Behavior]
Enforce=true                     ; actually disable exclusive mode
ExclusiveModeDisabled=false      ; stop touching the setting (monitor only)
```

Logs: `C:\ProgramData\Audio Watchdog\logs\audiowatchdog.log` (5 MB, keeps 5
rotated files). Important events are also posted to the Application event log
under source `Audio Watchdog`.

---

## How it works

The internal mechanism was reverse-engineered and **validated experimentally**
on a real device:

- The setting "Allow applications to take exclusive control" is a property on
  each endpoint's property store:
  - fmtid `{B3F8FA53-0004-438E-9003-51A46E139BFC}`, pid `3` — allow exclusive
  - fmtid `{B3F8FA53-0004-438E-9003-51A46E139BFC}`, pid `4` — exclusive priority
  - `PROPVARIANT` type `VT_UI4`; **`0` = blocked, any non-zero = allowed**
- It is written through the *public* Core Audio COM surface:
  `IMMDevice::OpenPropertyStore(STGM_READWRITE)` → `IPropertyStore::SetValue`
  → `Commit()`. No direct registry surgery is performed.
- The property keys themselves are not in the public SDK headers; the values
  come from reproducing the exact keys the Windows Sound control panel toggles
  and are confirmed by a behavioural probe.

**Behavioural proof (peformed on this machine):**
with the value set to `0`, a real `IAudioClient::Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE)`
returns `0x8889000E` (`AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED`); with any non-zero
value it returns `S_OK`. The watchdog writes `0` and then **re-reads** to make
sure it stuck, so it never relies on an unverified write.

**Persistence model:** the value is stored by the Windows audio policy engine,
not in user-editable registry hives on this machine, and there is **no**
reboot-safe guarantee that Windows/drivers keep it. That is exactly why the
watchdog exists: anything that re-enables the setting is caught again within
the notification cycle or the periodic fallback.

### Why a Windows service running as LocalSystem

The endpoint property store is machine-widely enforced by the audio policy
engine, and the guarantee must hold with no user logged on (the whole point).
Therefore the service runs as **LocalSystem** (0 dependencies) so it can read
the store owned by the audio services and re-apply the policy at every check.

---

## Architecture

```
src/
  main.cpp                 entry point, command routing (tray/service/CLI)
  app/                     system-tray resident (Shell_NotifyIcon, menu, autostart)
  service/                 Windows service (SCM, handlers, foreground mode)
  core/                    WatchdogEngine (scan loop, notify -> rescan)
  audio/                   MMDevice enumeration, IMMNotificationClient
                           watcher, ExclusiveModeManager, property-store I/O
  config/                  INI configuration
  logging/                 leveled file logger + rotation, event log
  cli/                     commands, diagnostics, WASAPI probe
  util/                    COM RAII, strings, paths, SCM wrappers
installer/                 self-contained C++ setup (RCDATA payload) + PS scripts
resources/                 app icon (logo.ico), version resource
tests/                     23 unit tests (mocked audio interfaces)
```

The audio layer is interface-based (`IAudioDeviceLister`, `IExclusiveModeStore`)
so the core logic is tested with in-memory mocks; the real implementations sit
on top of Core Audio and were verified live.

---

## License

AGPL-3.0 — see `LICENSE`.