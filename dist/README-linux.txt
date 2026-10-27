openMERC for Linux (x86_64)
===========================

openMERC is an open-source engine and game code for Soldier of Fortune (2000).
It contains no game data: you need your own licensed copy of Soldier of Fortune
(GOG or retail, updated to 1.07f).

Install
-------
1. Unpack the archive into your Soldier of Fortune folder, the one that holds
   base/pak0.pak:

       cd ~/Games/SoF                 # your game folder
       tar xzf openMERC-<version>-linux-x86_64.tar.gz

   This adds openMerc, openMerc-ded, openMERC.sh, a few .so files and the
   folder openmerc/ next to base/. No file of the game is replaced. The same
   archive also works unpacked into a folder inside the game folder
   (e.g. ~/Games/SoF/openMERC/); then the game data is found in the parent.
2. Start the game:

       ~/Games/SoF/openMerc

   openMERC.sh does the same and also lets you keep openMERC elsewhere: it reads
   the game folder from openmerc/game_path.txt, or asks for it once.

openMERC never writes into the game folder. Its own files are in openmerc/;
settings, saves, screenshots and the log go to openmerc/user/. To remove
openMERC, delete openmerc/ and the files the archive added. Dedicated server:
./openMerc-ded +map <map>.

Saves of the original game cannot be loaded by openMERC's game modules; the
load menu shows them greyed out as "Not compatible".

Required system libraries
-------------------------
Built on Debian 13; needs glibc 2.38 or newer and libstdc++ from GCC 13 or newer
(Ubuntu 24.04+, Debian 13+, Fedora 39+, Arch and similar).

  Bundled in openmerc/lib/:  libSDL3.so.0 (SDL 3.2.10, zlib licence)
                             libopenal.so.1 (OpenAL Soft 1.24.2, LGPL-2.0-or-later)
  From the system:  libc.so.6, libm.so.6, libstdc++.so.6, libgcc_s.so.1
                    OpenGL 3.3 driver: libGL.so.1 or libEGL.so.1 (Mesa or the
                      vendor driver), loaded at run time
                    X11 (libX11, libXext, libXcursor, libXi, libXfixes, libXrandr)
                      or Wayland (libwayland-client, libwayland-egl,
                      libwayland-cursor, libxkbcommon, optionally libdecor)
                    sound: PipeWire, PulseAudio or ALSA (libpipewire-0.3,
                      libpulse or libasound)

SDL loads the window and sound back ends at run time and uses whichever is
present. To use the system SDL 3 or OpenAL instead, delete the file from
openmerc/lib/.

Renderer: OpenGL 3.3 (default, vid_renderer gl3) or OpenGL ES 3 (gles3).
Only openMERC's own game modules run on Linux; the original game DLLs are
32-bit Windows code.

Problems and source code: https://github.com/sidreshot/openMERC
Licence: GPL-2.0-or-later (openmerc/LICENSE); third-party parts:
openmerc/THIRD_PARTY.md and openmerc/lib/LICENSE-*.txt.
