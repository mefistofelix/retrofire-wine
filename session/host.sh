#!/system/bin/sh
# host.sh GAME_DIR EXE WIDTH HEIGHT: started by the libretro core (outside the root file
# system). The game's folder becomes /game inside; fd 3 carries the input events.
D="$(cd "$(dirname "$0")" && pwd)"
export RF_BIND="$1"
exec sh "$D/run.sh" /bin/sh /usr/local/bin/rf-session "$2" "$3" "$4"
