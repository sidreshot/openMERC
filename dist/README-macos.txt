openMERC for macOS (Apple silicon, arm64)
=========================================

openMERC is an open-source engine and game code for Soldier of Fortune (2000).
It contains no game data: you need your own licensed copy of Soldier of Fortune
(GOG or retail, updated to 1.07f).

This build runs on Apple silicon only. Intel Macs are not included.
The original game and player modules are 32-bit Windows DLLs, so only openMERC's
own modules (this archive) run on a Mac.

Libraries
---------
The programs are linked to Homebrew and do not carry their own copies:

    brew install sdl3 openal-soft libomp

They look for these exact paths:

    /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib
    /opt/homebrew/opt/openal-soft/lib/libopenal.1.dylib
    /opt/homebrew/opt/libomp/lib/libomp.dylib

openMERC.app
------------
openMERC-<version>-macos-arm64-app.zip holds openMERC.app. Unzip it, move
openMERC.app wherever you like (Applications, for example) and open it.

On the first start it asks for your Soldier of Fortune folder: the one that
contains base/pak0.pak (for the GOG installer unpacked with innoextract, the
folder you unpacked it into). The folder is remembered; hold the Option key
while openMERC starts to choose it again.

Settings, saves, screenshots, the console history and a launcher.log are kept in

    ~/Library/Application Support/openMERC

openMERC writes nothing into the app or the game folder.

The app is not signed by Apple. The first time, macOS may refuse to open it:
Control-click openMERC.app, choose Open, then Open again (or allow it in
System Settings > Privacy & Security). The Homebrew libraries below are still
needed; the app says so when one is missing.

Extra engine arguments: open -a openMERC --args +map kos1

Install (plain folder)
----------------------
Unpack this archive anywhere that is not your game folder. The base/ directory
inside it is openMERC's modules (gamex86.dylib, player.dylib, zz_openmerc.pak).
Do not copy it over the game's base/ (that folder holds pak0.pak).

    tar xzf openMERC-<version>-macos-arm64.tar.gz
    cd openMERC-<version>-macos-arm64
    ./openMerc +set basedir "/path/to/Soldier of Fortune" +set vid_renderer gl3

Settings, saves and screenshots go to openmerc/user/ next to the executable.
openMERC does not write into the game folder.

The first time a map is loaded, omlight re-bakes its lighting in the background
(openmerc/user/lightmaps/). The new light is used from the next load of that map.
libomp is what lets that bake use more than one thread.

macOS OpenGL is 4.1. ref_gl3 asks for a 3.2 core context and runs on Apple's
OpenGL over Metal.
