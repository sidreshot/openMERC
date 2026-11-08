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
        hd/                      optional HD texture packs (*.pak), e.g.
                                 sofhd2024.pak from sof1.org
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
- `<exe dir>/openmerc/hd` and every `*.pak` in it are mounted above
  `openmerc` (base layer). An HD pack replaces textures by name with larger
  images; the renderer keeps texture coordinates on the size of the original
  (`r_retexturing`, `FS_ReadOriginalHead`). `om_hdtextures 0` passes the
  folder over without remounting, `vid_restart` reloads the images; the video
  menu shows the switch (Picture Quality, HD Textures) only when `om_hdpack`
  exists, i.e. the folder holds a pack. Installing a pack is copying its
  `.pak` there; nothing of the pack's own `autoexec.cfg` is needed.
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

## Game data from a disc image (2026-11-08)

When no folder with `base/pak0.pak` is found at start (the folders listed above), openMERC asks for the disc image
(`.iso`) of the player's own Soldier of Fortune copy (`client/om_import.c`). The installer on the disc is an
InstallShield 5 cabinet (`SETUP/DATA1.HDR` + `SETUP/DATA1.CAB`, the Platinum edition layout); every file of it that
belongs in a `base` folder (all `.pak` files, never `.exe`/`.dll`) is unpacked into `<game folder>/base`:
Windows - the folder of `openMerc.exe`, Android - the app's data folder (`basedir`). Nothing is downloaded.
`om_importiso <image> [folder]` does the same from the console; `+set om_noimport 1` skips the question.
The cabinet reader follows the layout unshield documents; it is tested on a synthetic image
(scratch generator: IS5 header/cabinet with stored and deflate-chunked files), not yet on a real disc.
