# retrofire-wine

Experiment for the RetroFire project: run Windows (x86) programs on the Fire TV Stick 4K Max
(32-bit ARM userland, Android 9) without emulating a whole PC.

* [Box86](https://github.com/ptitSeb/box86) translates the x86 code of the program to ARM and
  hands system and library calls to native ARM libraries.
* [Wine](https://www.winehq.org) (x86 build by [Kron4ek](https://github.com/Kron4ek/Wine-Builds))
  provides the Windows API.
* A minimal Debian armhf root file system gives them the glibc userland Android lacks; it is
  entered with `proot`, so no root access is needed.

## Stage 0: the probe

The workflow `probe.yml` builds Box86 for ARMv8 AArch32 and packs it with the root file system
and Wine into `retrofire-wine-probe.tar.gz` (release `probe`). `probe/run.sh` runs a command in
it from an adb shell:

    adb push retrofire-wine-probe.tar.gz /data/local/tmp/wb/
    adb shell "cd /data/local/tmp/wb && tar xzf retrofire-wine-probe.tar.gz && sh run.sh wine cmd /c ver"

The probe answers two questions only: does Windows code run on the stick this way, and how
fast compared with DOSBox Pure. Showing it on the TV from inside RetroArch (video, audio,
input as a libretro core) is a later, larger step.
