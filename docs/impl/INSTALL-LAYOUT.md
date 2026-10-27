# Install layout

openMERC is unpacked into the Soldier of Fortune folder (or into any folder
below it). It never replaces or writes a file of the game.

```
<Soldier of Fortune>/            the game (read only for openMERC)
    base/pak0.pak ...            game data
    user/                        the game's own user folder (not used)
    openMerc.exe (openMerc)      the engine, next to its runtime libraries
    openmerc/                    openMERC's own files
        gamex86.dll, player.dll  openMERC's game and player modules
                                 (gamex86.so, player.so on Linux)
        zz_openmerc.pak          openMERC's data
        openmerc-portable.txt    portable mode marker
        user/                    openMERC's user folder (config, saves,
                                 screenshots, logs), created on first start
```

On Linux the archive holds `openMerc`, `openMerc-ded`, `openMERC.sh`,
`OMsnd.so`, `ref_*.so` and `openmerc/` with the bundled libraries in
`openmerc/lib/` (the binaries look there first). The Windows archive holds
`openMerc.exe`, `openMerc-ded.exe` and their DLLs next to `openmerc/`.
None of these names exists in a Soldier of Fortune install.

The same archive can be unpacked into a subfolder, e.g.
`<Soldier of Fortune>/openMERC/`; then the game data is found in the parent.

## How the engine finds things

- Game data: without `-cddir`, the first of `<basedir>`, `<basedir>/..`,
  `<exe dir>`, `<exe dir>/..` that holds `base/pak0.pak` is the game folder.
- `<exe dir>/openmerc`, when it exists, is mounted above `base` (still in the
  base layer), so the game and player modules in it are found before the
  original ones in `base/`.
- Without `-user`, the user folder is `<exe dir>/openmerc/user`, whatever the
  current directory is. Portable-mode logs (stdout, stderr, console history,
  crash dumps) go there as well.
- With `-user <dir>` everything behaves as before.
- On Linux the engine starts from the folder of the binary (unless
  `-basedir`, `-datadir` or `-user` is given); `openMERC.sh` passes the game
  folder with `-cddir` when it is not the binary's folder.

Mode A (the original game modules) is chosen by removing or renaming
`openmerc/gamex86.dll` and `openmerc/player.dll`; the modules in `base/` are
then used.
