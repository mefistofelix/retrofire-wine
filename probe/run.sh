#!/system/bin/sh
# run.sh COMMAND...: run COMMAND inside the root file system (proot, no root needed).
# "wine ..." runs /opt/wine/bin/wine, a script that starts the x86 program with Box86.
# Run it from the RetroFire app's data folder (adb shell run-as com.retroarch.retrofire):
# the adb shell user may not create the Unix socket Wine's server needs in /data/local/tmp.
D="$(cd "$(dirname "$0")" && pwd)"
R="$D/rootfs"
L="$R/usr/lib/arm-linux-gnueabihf"
mkdir -p "$D/tmp" "$R/tmp" "$R/root"
export PROOT_TMP_DIR="$D/tmp"
export PROOT_NO_SECCOMP=1
unset LD_PRELOAD
case "$1" in
  wine|wineserver) set -- "/opt/wine/bin/$@" ;;
esac
exec "$L/ld-linux-armhf.so.3" --library-path "$L" "$R/usr/local/bin/proot" \
  -r "$R" -b "$R:$R" -b /dev -b /proc -b /sys -w /root \
  /usr/bin/env -i HOME=/root USER=root TERM=xterm LANG=C \
    PATH=/usr/local/bin:/usr/bin:/bin:/opt/wine/bin \
    WINEPREFIX=/root/.wine WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES="mscoree,mshtml=" \
    BOX86_LOG="${BOX86_LOG:-0}" BOX86_NOBANNER=1 DISPLAY= \
    "$@"
