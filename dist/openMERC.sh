#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# openMERC launcher for Linux. Needs a licensed Soldier of Fortune installation (GOG or retail 1.07f).
# Unpack openMERC into the Soldier of Fortune folder (the one that holds base/) or into a folder inside
# it and start this script (or openMerc itself), or put the game folder into openmerc/game_path.txt.
# openMERC never writes into the game folder: settings, saves, screenshots and logs go to openmerc/user/.
# Usage: ./openMERC.sh [extra engine arguments, e.g. +map kos1]
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE" || exit 1

has_paks() { # <dir>: true if <dir> has a base folder with pak0.pak (any letter case)
	[ -n "$1" ] && find "$1" -maxdepth 2 -ipath "$1/base/pak0.pak" 2>/dev/null | grep -q .
}

CFG=$HERE/openmerc/game_path.txt
SOF_DIR=
[ -f "$CFG" ] && SOF_DIR=$(head -n 1 "$CFG")
if ! has_paks "$SOF_DIR"; then
	for d in "$HERE" "$(cd "$HERE/.." && pwd)"; do
		if has_paks "$d"; then SOF_DIR=$d; break; fi
	done
fi
if ! has_paks "$SOF_DIR"; then
	echo "Soldier of Fortune data (base/pak0.pak) not found in $HERE or its parent folder."
	printf "Path to your Soldier of Fortune folder: "
	read -r SOF_DIR
	if ! has_paks "$SOF_DIR"; then
		echo "base/pak0.pak not found under \"$SOF_DIR\"." >&2
		exit 1
	fi
	mkdir -p "$HERE/openmerc"
	printf '%s\n' "$SOF_DIR" > "$CFG"
fi

# the game folder itself is the base folder; -cddir only when the game lives elsewhere
if [ "$SOF_DIR" = "$HERE" ]; then
	exec "$HERE/openMerc" -portable -basedir "$HERE" "$@"
fi
exec "$HERE/openMerc" -portable -basedir "$HERE" -cddir "$SOF_DIR" "$@"
