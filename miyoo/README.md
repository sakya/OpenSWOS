# OpenSWOS on Miyoo Mini

![OpenSWOS_001.png](screenshots/OpenSWOS_001.png)

## Build

Put the OpenSWOS Git checkout in the shared workspace of the
[Union Miyoo Mini toolchain](https://github.com/shauninman/union-miyoomini-toolchain).
Run `make shell` from the toolchain directory to enter the container. Inside
the container, go to the OpenSWOS repository root and run:

```sh
apt-get update
apt-get install -y autoconf automake libtool pkg-config cmake git zip
sh miyoo/build-sdl2.sh
sh miyoo/miyoo_build.sh
```

The container sets `CROSS_COMPILE`. The SDL2 script builds the Miyoo video and
audio drivers and puts the headers and libraries in `miyoo/include/` and
`miyoo/lib/`. Run it again when you need to rebuild SDL2. The OpenSWOS build
produces `bin/SWOS.miyoo`.

## Package and install

From the same repository root, run:

```sh
sh miyoo/miyoo_pack.sh
```

The packer copies the binary, SDL2 runtime libraries, launcher, icon, and
tracked game resources into `dist/miyoo/App/OpenSWOS/`. It also creates
`dist/miyoo/OpenSWOS.zip` with `App/` at the archive root.

Extract the ZIP at the root of your OnionOS SD card. On your computer, use
[OpenSWOS Setup](../INSTALL.md#4-run-openswos-setup) with your own copy of
Sensible World of Soccer 96/97. Copy the generated `assets/` folder to
`App/OpenSWOS/` so that `App/OpenSWOS/assets/ASSETS.ini` exists on the card.
Keep those assets on the card if you want to package a fresh build without
including them in the ZIP. The packer archives everything already under
`dist/miyoo/App/`.

Launch **OpenSWOS** from **Apps** in OnionOS. For startup errors, check
`App/OpenSWOS/launch.log` and `App/OpenSWOS/conf/.swos/SWOS.log`.

The D-pad moves the player. **A** and **B** fire, and **B** also skips.
**Start** pauses, **Select** shows statistics, **Y** requests an instant
replay, and **X** requests the replay after a match. Use **L2** for Escape,
**Select + Start** to quit OpenSWOS, and **Menu + Power** for OnionOS screenshots.
