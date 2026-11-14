#!/system/bin/sh
# run.sh COMMAND...: run COMMAND inside the root file system (proot, no root needed).
# "wine ..." is started through Box86.
D="$(cd "$(dirname "$0")" && pwd)"
R="$D/rootfs"
L="$R/usr/lib/arm-linux-gnueabihf"
mkdir -p "$D/tmp" "$R/tmp" "$R/root"
export PROOT_TMP_DIR="$D/tmp"
export PROOT_NO_SECCOMP=1
unset LD_PRELOAD
case "$1" in
  wine|wineserver|wineboot|winecfg) set -- box86 "/opt/wine/bin/$@" ;;
esac
exec "$L/ld-linux-armhf.so.3" --library-path "$L" "$R/usr/bin/proot" \
  -r "$R" -b /dev -b /proc -b /sys -w /root \
  /usr/bin/env -i HOME=/root USER=root TERM=xterm LANG=C \
    PATH=/usr/local/bin:/usr/bin:/bin:/opt/wine/bin \
    WINEPREFIX=/root/.wine WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES="mscoree,mshtml=" \
    BOX86_LOG="${BOX86_LOG:-0}" BOX86_NOBANNER=1 DISPLAY= \
    "$@"
