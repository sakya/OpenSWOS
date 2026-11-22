#!/bin/sh
# Package an existing Miyoo build as dist/miyoo/App/OpenSWOS.
set -eu
cd "$(dirname "$0")/.."

pack_dir=dist/miyoo
out=$pack_dir/App/OpenSWOS
archive=$pack_dir/OpenSWOS.zip
set -- miyoo/lib/libSDL2-2.0.so.0 miyoo/lib/libEGL.so miyoo/lib/libGLESv2.so
icon=packaging/linux/hicolor/64x64/apps/openswos.png

if ! command -v zip >/dev/null 2>&1; then
    printf 'Missing required command: zip\n' >&2
    exit 1
fi

for file in bin/SWOS.miyoo miyoo/launch.sh "$icon" "$@"; do
    if [ ! -f "$file" ]; then
        printf 'Missing required file: %s\n' "$file" >&2
        exit 1
    fi
done

# Keep the tracked file list NUL-delimited and propagate git failures before copying.
manifest=$(mktemp)
archive_tmp=
trap 'rm -f "$manifest"; if [ -n "$archive_tmp" ]; then rm -f "$archive_tmp"; fi' 0
git ls-files -z -- bin/data bin/data_json bin/grafs bin/grafs_aga bin/grafs_arcade bin/mods bin/sound bin/sound_dos > "$manifest"

mkdir -p "$out/lib/miyoo"
cp bin/SWOS.miyoo "$out/"
cp -L "$@" "$out/lib/miyoo/"
sed 's/\r$//' miyoo/launch.sh > "$out/launch.sh"
chmod +x "$out/SWOS.miyoo" "$out/launch.sh"
cp "$icon" "$out/icon.png"
cat > "$out/config.json" <<'JSON'
{"label":"OpenSWOS","icon":"icon.png","launch":"launch.sh","description":"Sensible World of Soccer"}
JSON

xargs -0 sh -ec '
    out=$1
    shift
    for file do
        dest="$out/${file#bin/}"
        mkdir -p "$(dirname "$dest")"
        cp "$file" "$dest"
    done
' sh "$out" < "$manifest"

archive_tmp=$(mktemp "$pack_dir/.OpenSWOS.XXXXXX")
(cd "$pack_dir" && zip -qr - App) > "$archive_tmp"
# The container may own the archive, while WSL exposes it as the Debian user.
chmod 0644 "$archive_tmp"
mv -f "$archive_tmp" "$archive"

printf 'Created %s\n' "$archive"
printf 'Copy %s to App/ on the SD card, then add your assets folder.\n' "$out"
