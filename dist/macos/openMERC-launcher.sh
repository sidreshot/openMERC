#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# openMERC.app launcher (Contents/MacOS/openMERC-launcher, the bundle's CFBundleExecutable).
#
# Finds the Soldier of Fortune folder (the one that holds base/pak0.pak), asks for it with the system folder
# picker when it is not known yet or no longer holds the game, remembers it, and starts openMerc from the bundle.
# Settings, saves, screenshots and logs go to ~/Library/Application Support/openMERC, never into the bundle
# (an unsigned app may run from a read-only, translocated copy) and never into the game folder.
#
# Hold the Option key while the app starts to choose the game folder again.
# Extra arguments are passed on to openMerc (open -a openMERC --args +map kos1).

HERE=$(cd "$(dirname "$0")" && pwd)
SUPPORT="$HOME/Library/Application Support/openMERC"
CFG="$SUPPORT/game_path.txt"
LOG="$SUPPORT/launcher.log"

mkdir -p "$SUPPORT"

alert() { # <message>: a modal message box (the app has no terminal)
	osascript - "$1" >/dev/null 2>&1 <<-'EOF'
		on run argv
			display alert "openMERC" message (item 1 of argv) as critical buttons {"OK"} default button "OK"
		end run
	EOF
}

has_paks() { # <dir>: true if <dir> has a base folder with pak0.pak (any letter case)
	[ -n "$1" ] && [ -d "$1" ] && find "$1" -maxdepth 2 -ipath "$1/base/pak0.pak" 2>/dev/null | grep -q .
}

option_held() { # true while the Option key is down
	flags=$(osascript -l JavaScript -e 'ObjC.import("AppKit"); $.NSEvent.modifierFlags' 2>/dev/null) || return 1
	[ $((flags & 524288)) -ne 0 ] # NSEventModifierFlagOption
}

choose_folder() { # prints the chosen folder (POSIX path, no trailing slash); fails on cancel
	osascript <<-'EOF' 2>/dev/null | sed 's#/$##'
		set p to choose folder with prompt "Choose your Soldier of Fortune folder (the one that contains the base folder with pak0.pak):"
		POSIX path of p
	EOF
}

# the Homebrew libraries the programs are linked to (README.txt)
for lib in /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib /opt/homebrew/opt/openal-soft/lib/libopenal.1.dylib \
		/opt/homebrew/opt/libomp/lib/libomp.dylib; do
	if [ ! -f "$lib" ]; then
		alert "A library openMERC needs is missing:
$lib

Install the libraries with Homebrew (https://brew.sh) in Terminal:
brew install sdl3 openal-soft libomp"
		exit 1
	fi
done

SOF_DIR=
[ -f "$CFG" ] && SOF_DIR=$(head -n 1 "$CFG")
option_held && SOF_DIR=

while ! has_paks "$SOF_DIR"; do
	SOF_DIR=$(choose_folder) || exit 0 # cancelled
	if ! has_paks "$SOF_DIR"; then
		alert "No base/pak0.pak was found in
$SOF_DIR

Choose the folder of your Soldier of Fortune installation (GOG, or retail updated to 1.07f): the one that contains the base folder."
		SOF_DIR=
	fi
done
printf '%s\n' "$SOF_DIR" > "$CFG"

# -basedir: the bundle's Resources, which hold openMERC's own base/ (game and player modules, zz_openmerc.pak);
# -cddir: the game's paks, only read; -user: everything openMERC writes;
# -cfgdir (relative to $HOME): the engine's home folder (console history), else ~/.local/share/openMERC,
# which is a fatal error when ~/.local/share does not exist
RES=$(cd "$HERE/../Resources" && pwd)
cd "$HERE" || exit 1
echo "== $(date '+%Y-%m-%d %H:%M:%S') game folder: $SOF_DIR" >> "$LOG"
exec "$HERE/openMerc" -basedir "$RES" -cddir "$SOF_DIR" -user "$SUPPORT" -cfgdir "Library/Application Support/openMERC" \
	+set vid_renderer gl3 "$@" >> "$LOG" 2>&1
