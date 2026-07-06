# diskscangrok 💾

WizTree-style **TUI disk space analyzer** for Windows, built in C++20 + FTXUI.
Runs beautifully from **PowerShell** (or Windows Terminal / cmd).

Goal: single **portable .exe** — no install, no VC++ redist, just copy and run.

> **Status**: Core TUI + Win32 scanner working. MFT fast path has been repaired and is now the **default** for drive-root scans when running as Administrator (the fast WizTree-like mode). Set `DISKSCAN_USE_MFT=0` to force the slower but always-reliable Win32 walker.

## Quick Start (PowerShell)

```powershell
# Build (see below) or download the portable exe

.\diskscangrok.exe                 # interactive drive/folder picker
.\diskscangrok.exe C:\             # scan a drive (run as Admin for best speed)
.\diskscangrok.exe "D:\Users\Neil MacNeil\Documents"
```

Keyboard (same in picker and tree):

- `↑` `↓` / `j` `k` — move
- `Enter` / `→` — scan / expand
- `←` — collapse / parent
- `r` — rescan
- `q` / `Esc` — quit

## Features

- Live updating tree sorted by size (largest first)
- Size bars + percentages + human sizes (KiB/MiB/GiB)
- Async scan, UI stays responsive
- Drive picker with volume labels (Windows)
- Falls back gracefully when not elevated
- Correct "size on disk" using `GetCompressedFileSizeW`
- Long path support (`\\?\`)

## Building a Portable Single EXE

**Recommended (Visual Studio 2022 Build Tools + Ninja)**

Open "x64 Native Tools Command Prompt for VS 2022" or Developer PowerShell, then:

```powershell
cd path\to\diskscangrok
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# The portable exe is at:
#   build\diskscangrok.exe
```

Or use the helper:

```powershell
.\scripts\build-portable.ps1
```

The script produces a static-linked exe in `dist\diskscangrok.exe` (no external CRT DLLs).

### Prerequisites (one-time)

```powershell
winget install Git.Git Kitware.CMake Ninja-build.Ninja Microsoft.VisualStudio.2022.BuildTools
```

Run as Administrator to get the fast MFT path on drive roots (recommended for full C: etc.).

Cross-compile note: see `cmake/toolchain-mingw64.cmake`.

## Why not the fast MFT path by default?

The original MFT reader prototype does not completely populate the tree / sizes for many volumes.
We ship a correct Win32 implementation that works everywhere. MFT repair is tracked as follow-up work.

## Controls & UI

See in-app status bar. The app is designed to feel like WizTree but inside your favorite terminal (PowerShell).

## Architecture (high level)

- `Scanner` + (Win32 walker or experimental MFT) → `FileNode` tree
- FTXUI TUI: `App` (picker + main view) + `TreeView` + `StatusBar`
- Everything compiled statically for true portability.

## Verification

After build:

```powershell
ctest --test-dir build --output-on-failure
.\build\diskscangrok.exe build\diskscangrok
# Check with: dumpbin /dependents .\build\diskscangrok.exe  (only system DLLs)
```

## License

MIT or similar (to be confirmed). Enjoy finding what's eating your disk from inside PowerShell!

---
Repo: https://github.com/neilmacneil123/diskscangrok

