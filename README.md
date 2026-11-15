# retrofire-wine

Experiment for the RetroFire project: run Windows (x86) programs on the Fire TV Stick 4K Max
(32-bit ARM userland, Android 9) without emulating a whole PC.

* [Box86](https://github.com/ptitSeb/box86) translates the x86 code of the program to ARM and
  hands system and library calls to native ARM libraries.
* [Wine](https://www.winehq.org) (x86 build by [Kron4ek](https://github.com/Kron4ek/Wine-Builds))
  provides the Windows API.
* A minimal Debian armhf root file system gives them the glibc userland Android lacks; it is
  entered with `proot` (Termux's fork, which knows Android), so no root access is needed.

## Stage 0: the probe

The workflow `probe.yml` builds Box86 for ARMv8 AArch32 and packs it with the root file system
and Wine into `retrofire-wine-probe.tar.gz` (release `probe`). `probe/run.sh` runs a command in
it from an adb shell:

    adb push retrofire-wine-probe.tar.gz /data/local/tmp/wb/
    adb shell "cd /data/local/tmp/wb && tar xzf retrofire-wine-probe.tar.gz && sh run.sh wine cmd /c ver"

The probe answers two questions only: does Windows code run on the stick this way, and how
fast compared with DOSBox Pure. Showing it on the TV from inside RetroArch (video, audio,
input as a libretro core) is a later, larger step.

## Result of the probe (Fire TV Stick 4K Max, 15 Nov 2026)

Run as the RetroFire app user in its data folder (`adb shell run-as com.retroarch.retrofire`):

* (Measured with the first Box86 build, whose clocks were wrong: start time and benchmark figures below are not reliable; with the fixed build Wine starts in about 8 s and Pinball is on screen 20 s after launch.)
* `wine cmd /c ver` answers `Microsoft Windows 10.0.19043`: Wine 9.0 x86 runs under Box86
  v0.3.8 (dynarec for ARMv8 AArch32) inside the proot'ed Debian.
* Starting Wine takes about 65-70 s every time (its server and services, and proot traps
  every system call).
* `probe/bench.c` (Win32 console, `-O2`): 2000M iterations of a small integer loop in 13 s,
  the sieve in 5 s, a 1600x1600 Mandelbrot (x87 floating point) in 4 s. That is roughly
  1000, 600 and 350 million x86 instructions per second; DOSBox Pure's recompiler reaches
  about 40 million (40000 cycles per millisecond) on the same stick.

What had to be worked around:

* Debian's proot keeps its loader in `/tmp`, which Android lacks: Termux's fork is built.
* The kernel cannot start x86 programs: `wine` and `wineserver` are scripts that call Box86,
  and `wine-preloader` is disabled.
* The adb shell user cannot create Unix sockets in `/data/local/tmp` (Wine's server needs
  one): the bundle runs from the app's own data folder, where a libretro core would live.

## Stage 1: on the TV, inside RetroArch

`core/libretro.c` is a libretro core ("RetroFire Wine", content: an `.exe`). It starts
`host.sh` from the bundle in `<app data>/wb`; inside the root file system `rf-session` runs a
virtual X display (Xvfb) whose frame buffer is a file the core maps and shows, the helper
`rf-input` that turns the core's keyboard, mouse and pad events into X events, and the program
in a Wine desktop of 640x480.

3D Pinball (the Windows XP build) is the test program: it shows on the TV and takes keys sent
through RetroArch.

What had to be worked around here:

* Inside the app Android filters system calls and kills the glibc programs ("Bad system
  call"): proot is Termux's Android build (`host/`), which answers "not implemented" instead.
  It runs with `-0` (the X server wants to be root to compile its keymap) and
  `--link2symlink` (its lock file is a hard link).
* A program started in the Wine desktop is not the active window until the focus changes
  once: `rf-session` moves the focus to the desktop and back.
* Box86 built with Ubuntu's ARM compiler got a 64-bit `time_t`, and every clock an x86
  program read lost its nanoseconds: Windows time moved in steps of one second and the game
  ran in slow motion. `probe/timing.c` shows it; Box86 is now built with the 32-bit one.

Not done: sound, a shorter start, installing the bundle and the games through `rf`.
