# Desktop Restore for Windows ARM64

A native ARM64 port of Desktop Restore 1.7.2 by Jamie O'Connell (midiox.com). It saves and restores the desktop icon layout for each screen resolution and monitor setup.

## Why it is a new program

The original is a shell extension DLL (x86 and x64 only). On Windows on Arm, Explorer is a native ARM64 process and can only load ARM64 shell extensions, so the original's menu never appears. There is no source code, so this is a clean re-implementation. It is a standalone ARM64 .exe that works with Explorer from outside its process, and it adds the same right-click menu through registry entries.

## Files

| Path | Purpose |
|---|---|
| `DesktopRestore-ARM64\DesktopRestore.exe` | For the ARM64 PC |
| `DesktopRestore-x64\DesktopRestore.exe` | Same program for x64 PCs |
| `Source\` | C++ source, unit tests, `build.sh` |

## Install, use, remove

1. Copy `DesktopRestore.exe` to a permanent folder, for example `C:\Tools\DesktopRestore`.
2. Double-click it and answer **Yes**. This adds **Desktop Restore** to the desktop right-click menu for your user only. No admin rights are needed.
3. On Windows 11, right-click the desktop, choose **Show more options**, then **Desktop Restore**.

The commands are the same as the original's: Save Desktop, Restore Desktop, Custom Save/Restore (named layouts, delete, `.dtr` files), Highlight Unmatched Icons, and Settings. Restoring needs **Auto arrange icons** off; if it is on, the program asks and then turns it off, as the author's DesktopCmd does. With `/q` or `/y` it turns it off without asking.

To remove it, go to **Settings > Apps > Installed apps > Desktop Restore**, or run `DesktopRestore.exe /uninstall`. Your saved layouts are kept.

## Compatibility with the original

Checked against the latest original, 1.7.2.083 (October 2022), and its command-line tool DesktopCmd.exe. The storage is the same as in 1.7.0, so layouts saved with either version work as they are, in both directions:
- registry key: `HKCU\Software\JOConnell\DeskTop`
- layout names, such as `Layout 1920 x 1080 (48)` and `Multi-Monitor *[1920 x 1080][2560 x 1440] (48)`
- settings and the `.dtr` file format

To bring layouts over from the old PC:
1. On the old PC, choose **Custom Save/Restore > Save to File**.
2. Copy the `.dtr` file to the ARM64 PC.
3. On the ARM64 PC, choose **Custom Save/Restore > Restore from File**.

Icons are matched by full path first, then by file name, then by label. Matching still works when the user folder is different or the Desktop has moved into OneDrive.

## Changes from the original

- It runs outside Explorer, so a fault can't take down the desktop or taskbar. To update it, replace the .exe.
- The menu appears on the desktop only, not on every folder background.
- It has a command line for scripts and Task Scheduler. Run `DesktopRestore.exe /?` for the list:
  - `/save [layout]` and `/restore [layout]`
  - `/savefile file.dtr` and `/restorefile file.dtr`
  - `/q` for no prompts
  - DesktopCmd syntax also works: `save | restore | query [/n "Layout" | /f "file.dtr"] [/y]`. Unlike DesktopCmd it is a windowed program: it prints nothing (check the exit code), `query` opens the Custom dialog, and PowerShell should start it with `Start-Process -Wait`. Batch files and Task Scheduler wait for it anyway.
- Icons that are not in the layout but sit where a restored icon goes are moved to a free spot, as in 1.7.2. Icons that are off screen, or whose monitor is no longer connected, also go to a free spot instead of stacking on another icon.
- A failed or empty capture never replaces a saved layout.
- Saving over a layout for a different resolution asks first.
- `/diag` writes a troubleshooting report and changes nothing.

## Test status

- Tested on Windows 11 x64 and Windows 11 ARM64 (October 9, 2026): save, restore, and positions kept after F5.
- It builds with llvm-mingw as a native ARM64 executable (machine type AA64) that needs no extra runtime DLLs.
- 122 unit test checks pass. They cover:
  - registry and `.dtr` formats, including files in the original's exact format
  - layout naming
  - icon matching
  - the restore algorithm, run against a simulated desktop
  - the command line, including DesktopCmd syntax

## Troubleshooting

- **Something wrong:** run `DesktopRestore.exe /diag`. It writes `Documents\DesktopRestore\diagnostics.txt` and changes nothing.
- **"Windows protected your PC":** the .exe is unsigned. Choose **More info > Run anyway**. If Smart App Control is on, it blocks unsigned programs entirely.
- **Running as administrator:** don't. The program has to run as you to reach Explorer.
- **Original still installed on an x64 PC:** don't add this menu there too, or you get two Desktop Restore menus.

## Build

Run `LLVM_MINGW=/path/to/llvm-mingw ./build.sh`. It was built with llvm-mingw 20261006 and creates both executables and `tests.exe`.
