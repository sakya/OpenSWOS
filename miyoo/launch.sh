#!/bin/sh
cd "$(dirname "$0")" || exit 1
exec > launch.log 2>&1

export HOME="$PWD/conf"
export SWOS_ASSETS="$PWD/assets"
export LD_LIBRARY_PATH="$PWD/lib/miyoo${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$HOME"

audio=/mnt/SDCARD/.tmp_update/script/stop_audioserver.sh
[ ! -f "$audio" ] || . "$audio"

./SWOS.miyoo --renderer=sdl
