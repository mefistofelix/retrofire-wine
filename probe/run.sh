#!/system/bin/sh
# run.sh COMMAND...: run COMMAND inside the root file system (proot, no root needed).
# With RF_BIND=<folder> that folder is /game inside.
# "wine ..." runs /opt/wine/bin/wine, a script that starts the x86 program with Box86.
# Run it from the RetroFire app's data folder (adb shell run-as com.retroarch.retrofire):
# the adb shell user may not create the Unix socket Wine's server needs in /data/local/tmp.
D="$(cd "$(dirname "$0")" && pwd)"
R="$D/rootfs"
mkdir -p "$D/tmp" "$R/tmp" "$R/root"
# host/proot is Termux's Android build: it runs outside the root file system, with bionic.
# Inside an app Android filters system calls (seccomp) and kills a process that makes one it
# does not know; the glibc inside does (rseq, clone3, the 64-bit time calls). This proot
# answers "not implemented" for those instead, and glibc falls back to the old calls.
export PROOT_TMP_DIR="$D/tmp"
export PROOT_LOADER="$D/host/loader"
export LD_LIBRARY_PATH="$D/host"
unset LD_PRELOAD
case "$1" in
  wine|wineserver) set -- "/opt/wine/bin/$@" ;;
esac
exec "$D/host/proot" -0 --link2symlink \
  -r "$R" -b "$R:$R" ${RF_BIND:+-b} ${RF_BIND:+"$RF_BIND:/game"} -b /dev -b /proc -b /sys -w /root \
  /usr/bin/env -i HOME=/root USER=root TERM=xterm LANG=C \
    PATH=/usr/local/bin:/usr/bin:/bin:/opt/wine/bin \
    WINEPREFIX=/root/.wine WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES="mscoree,mshtml=" \
    BOX86_LOG="${BOX86_LOG:-0}" BOX86_NOBANNER=1 DISPLAY= \
    "$@"
