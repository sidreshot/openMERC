#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# openMERC launcher for Linux. Needs a licensed Soldier of Fortune installation (GOG or retail 1.07f).
# Unpack openMERC into a folder inside the game folder (next to base/) and start this script, or put the
# game folder into game_path.txt next to this file. openMERC never writes into the game folder: settings,
# saves, screenshots and logs go to ./user/.
# Usage: ./openMERC.sh [extra engine arguments, e.g. +map kos1]
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE" || exit 1

has_paks() { # <dir>: true if <dir> has a base folder with pak0.pak (any letter case)
	[ -n "$1" ] && find "$1" -maxdepth 2 -ipath "$1/base/pak0.pak" 2>/dev/null | grep -q .
}

SOF_DIR=
[ -f game_path.txt ] && SOF_DIR=$(head -n 1 game_path.txt)
if ! has_paks "$SOF_DIR"; then
	SOF_DIR=$(cd "$HERE/.." && pwd)
	if ! has_paks "$SOF_DIR"; then
		echo "Soldier of Fortune data (base/pak0.pak) not found in $SOF_DIR."
		printf "Path to your Soldier of Fortune folder: "
		read -r SOF_DIR
		if ! has_paks "$SOF_DIR"; then
			echo "base/pak0.pak not found under \"$SOF_DIR\"." >&2
			exit 1
		fi
		printf '%s\n' "$SOF_DIR" > game_path.txt
	fi
fi

exec "$HERE/openMerc" -portable -basedir "$HERE" -cddir "$SOF_DIR" "$@"
