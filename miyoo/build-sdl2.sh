#!/bin/sh
# Prepares SDL2 headers and libraries to build COK for Miyoo Mini (Onion),
# using sakya's port (https://github.com/sakya/miyoo-sdl2), based on
# steward-fu's one with the MI_GFX/MI_AO drivers built from source.
# Run it inside the union-miyoomini-toolchain container.
#
# Usage (inside the container, from the project folder):
#   apt-get update && apt-get install -y autoconf automake libtool pkg-config cmake
#   sh miyoo/build-sdl2.sh
#   make re PLATFORM=miyoo
#
# Output (next to this script):
#   miyoo/include/SDL2  SDL2, SDL2_image, SDL2_ttf and SDL2_mixer headers
#   miyoo/lib           libraries used for linking, to be copied to
#                       App/COK/lib on the Miyoo
#
# Optional variables:
#   BUILD  temporary work folder (default: $HOME/sdl2-build)
#   SRC    clone of sakya/miyoo-sdl2 (default: $BUILD/miyoo-sdl2)
#   JOBS   parallel build jobs (default: 4)
set -e

: "${CROSS_COMPILE:?CROSS_COMPILE is not set: run this script inside the toolchain container}"

MIYOO=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:-$HOME/sdl2-build}
SRC=${SRC:-$BUILD/miyoo-sdl2}
PREFIX=$BUILD/install
HOST=arm-linux-gnueabihf
JOBS=${JOBS:-4}

export CC=${CROSS_COMPILE}gcc
export CXX=${CROSS_COMPILE}g++
export AR=${CROSS_COMPILE}ar
export RANLIB=${CROSS_COMPILE}ranlib
export STRIP=${CROSS_COMPILE}strip
export CFLAGS="-O2 -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard"
# pkg-config must only see the libraries built here
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig
export PKG_CONFIG_PATH=
export PATH=$PREFIX/bin:$PATH

mkdir -p "$BUILD"
if [ ! -d "$SRC" ]; then
  git clone --depth 1 -c core.autocrlf=false https://github.com/sakya/miyoo-sdl2 "$SRC"
fi
# sdl2-master holds the steward-fu tree: SDL2 sources, MI SDK (mini/),
# prebuilt EGL/GLESv2 and the SDL2_* dependency tarballs
TREE=$SRC/sdl2-master
DEP=$TREE/sdl2/dependency
rm -rf "$PREFIX"

# 1. SDL2 with the Miyoo Mini video (MI_GFX) and audio (MI_AO) drivers.
#    The mini driver links libEGL/libGLESv2 from prebuilt/640x480 and the
#    MI libraries from mini/lib (these are already on the device).
cd "$TREE/sdl2"
sh ./autogen.sh
rm -rf "$BUILD/sdl2" && mkdir -p "$BUILD/sdl2" && cd "$BUILD/sdl2"
LDFLAGS="-L$TREE/mini/lib" "$TREE/sdl2/configure" --host=$HOST --prefix="$PREFIX" \
  --disable-static --enable-shared \
  --enable-video-mini --enable-audio-mini \
  --disable-video-x11 --disable-video-wayland --disable-video-kmsdrm \
  --disable-video-vulkan --disable-video-opengl --disable-video-opengles \
  --disable-video-opengles2 \
  --disable-pulseaudio --disable-pipewire --disable-alsa --disable-oss \
  --disable-jack --disable-sndio --disable-esd --disable-arts --disable-nas \
  --disable-dbus --disable-ime --disable-ibus --disable-fcitx \
  --disable-libudev --disable-hidapi --disable-joystick-virtual
make -j$JOBS
make install

build_dep() {
  name=$1; shift
  rm -rf "$BUILD/$name"
  tar xzf "$DEP/$name.tar.gz" -C "$BUILD"
  cd "$BUILD/$name"
  ./configure --host=$HOST --prefix="$PREFIX" --disable-static "$@"
  make -j$JOBS
  make install
}

# The other libraries link against libSDL2, which needs the EGL/MI libraries
export LDFLAGS="-Wl,-rpath-link,$TREE/prebuilt/640x480 -Wl,-rpath-link,$TREE/mini/lib"

# 2. SDL2_image: PNG/JPG with built-in stb_image, no dependencies
build_dep SDL2_image-2.8.1 \
  --enable-stb-image --disable-avif --disable-jxl --disable-tif --disable-webp

# 3. SDL2_ttf with built-in FreeType: no libfreetype/libpng/libbz2 needed
build_dep SDL2_ttf-2.20.2 --enable-freetype-builtin --disable-harfbuzz

# 4. SDL2_mixer: WAV, OGG (stb_vorbis), MP3 (minimp3), FLAC (dr_flac)
build_dep SDL2_mixer-2.6.3 \
  --disable-music-mod --disable-music-midi-fluidsynth --disable-music-opus \
  --disable-music-ogg-tremor --disable-music-flac-libflac \
  --disable-music-mp3-mpg123

# 5. Copy into the project: headers to miyoo/include/SDL2, libraries to
#    miyoo/lib (real files, no symlinks, named as loaded at runtime)
rm -rf "$MIYOO/include/SDL2"
mkdir -p "$MIYOO/include" "$MIYOO/lib"
cp -r "$PREFIX/include/SDL2" "$MIYOO/include/SDL2"
rm -f "$MIYOO"/include/SDL2/SDL_test*.h

L=$MIYOO/lib
# libjson-c was only needed by steward-fu's prebuilt libSDL2
rm -f "$L/libjson-c.so.5"
cp "$TREE/prebuilt/640x480/libEGL.so" "$TREE/prebuilt/640x480/libGLESv2.so" "$L/"
for f in libSDL2-2.0.so.0 libSDL2_image-2.0.so.0 libSDL2_ttf-2.0.so.0 libSDL2_mixer-2.0.so.0; do
  cp -L "$PREFIX/lib/$f" "$L/$f"
  $STRIP "$L/$f"
done

echo
echo "Done."
echo "  Headers:   $MIYOO/include/SDL2"
echo "  Libraries: $L"
ls -l "$L"
echo
echo "Now build with: make re PLATFORM=miyoo"
