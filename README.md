# openMERC

**English** | [Русский](#русский)

openMERC is an open-source (GPL v2) engine for **Soldier of Fortune** (Raven Software, 2000). It is written
clean-room on top of [Yamagi Quake II](https://github.com/yquake2/yquake2) and runs the original game data on
current systems: any resolution, widescreen HUD and menus, an OpenGL 3.2 renderer with optional modern effects,
and an OpenAL sound module.

> **Work in progress.** The engine, renderer, sound and menus are usable; openMERC's own game and player logic
> (mode B) loads and runs all 32 campaign maps but is not finished. Expect bugs - see [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

## You need your own copy of the game

openMERC contains **no files of the original game**. You need a legally owned copy of Soldier of Fortune (the GOG
release, or a retail CD updated to 1.07f). openMERC reads the game's `.pak` files from your installation and never
writes into it.

## Status

- **Engine**: all 75 stock maps load and render; Ghoul models, fog, terrain, sky, effects; save/load and level
  changes; console always available.
- **Graphics**: defaults at maximum quality; bloom on emissive surfaces, volumetric fog, soft sun shadows,
  per-pixel dynamic lights, soft particles and other effects, each with its own switch in the Video menu (some are
  still being tuned).
- **Menus**: a new widescreen menu with free fonts (Play, JetBrains Mono) that scale to any resolution and cover
  Latin and Cyrillic text. The parental lock is removed; violence is always at full level.
- **Movement**: Quake II player movement with SoF's differences.
- **Two ways to play**:
  - *mode A* - openMERC engine + the original `gamex86.dll` / `player.dll` from your installation (the most
    complete campaign today);
  - *mode B* - everything open source (engine, renderer, sound, game and player modules). All 32 campaign maps
    load and run; AI, some scripted sequences and some weapon details are unfinished.
- **Multiplayer**: openMERC servers and clients play with each other. Original 1.07f servers, clients and demos are
  **not** compatible (see *Clean room*), and no compatibility mode is planned.
- **Platforms**: Windows (32-bit engine on 64-bit Windows 10/11). Linux build scripts are included
  (`tools/build/build-linux.sh`, [docs/BUILD-linux.md](docs/BUILD-linux.md)) and are experimental.

## Build (Windows)

Requirements:

- Visual Studio 2022 Build Tools (MSVC **x86**), CMake 3.20+, Ninja;
- vcpkg in classic mode with `sdl3:x86-windows` and `openal-soft:x86-windows`;
- Python 3.10+ (data archive, tools, tests).

Set `VCVARSALL` (default `C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat`) and `OM_VCPKG` (your vcpkg root).
`OM_BUILD_DIR` selects a build directory. From the repository root in `cmd.exe`:

```
tools\build\build-msvc.cmd x86 Release   engine: openMerc.exe, openMerc-ded.exe, ref_gl3.dll, base data zz_openmerc.pak
modules\sound\build.cmd Release          sound module OMsnd.dll (+ OpenAL32.dll, fmt.dll)
game\build.cmd Release                   game module gamex86.dll
player\build.cmd Release                 player module player.dll
```

Assemble a game folder: the engine files and the sound module side by side, `gamex86.dll`, `player.dll` and
`zz_openmerc.pak` in `base\`, and `dist\openMERC.cmd` on top. Start `openMERC.cmd`; on the first start it asks for
your Soldier of Fortune folder (the one that contains `base\pak0.pak`). Settings, saves, screenshots and logs go to
`user\` next to the launcher. For mode A delete `gamex86.dll` and `player.dll` from openMERC's `base\`.

Unit tests built with the engine and modules: `sofpm_test` (player movement), `omghoul_test` (Ghoul model
library), `pm_unit` (player module rules). `python tools/legal_scan.py` checks the tree for copied tables and
forbidden markers.

## Repository layout

| Directory | Content |
|---|---|
| `engine/` | the engine: Yamagi Quake II base + SoF client, server, Ghoul model system, GL3 renderer, menus |
| `modules/sound/` | sound module `OMsnd.dll` (OpenAL Soft) |
| `game/` | game module `gamex86.dll`: world, AI, damage and gore, navigation, bots, scripts, props, game modes, saves |
| `player/` | player module `player.dll`: weapons, inventory, prediction |
| `data/` | openMERC's own data files (menus, strings, fonts), packed into `base\zz_openmerc.pak` |
| `docs/specs/` | behaviour specifications the implementation is written from |
| `docs/` | clean-room record, deviations from the original, known issues, implementation journals (`docs/impl/`) |
| `tools/`, `tests/` | format readers, pak tools, build scripts, black-box test harnesses |

## Clean room

See [CLEANROOM.md](CLEANROOM.md) and [docs/LEGAL_NOTES.md](docs/LEGAL_NOTES.md). The code was written from
behaviour specifications, the GPL Quake II sources and public documentation; no code of the original executables
is included. The tree contains no data tables copied from the original executables, and the engine never reads
`SoF.exe`. Only the game data (`*.pak`) of a legally owned copy is used.

## License

GPL v2 or later, see [LICENSE](LICENSE). Third-party components and their licenses are listed in
[THIRD_PARTY.md](THIRD_PARTY.md) (Yamagi Quake II and id Software's Quake II sources, SDL3, OpenAL Soft, fmt,
stb, the OFL fonts). Soldier of Fortune is a trademark of its owners; openMERC is not affiliated with Raven Software
or Activision.

---

## Русский

openMERC - открытый (GPL v2) движок для **Soldier of Fortune** (Raven Software, 2000). Он написан по методу
«чистой комнаты» на основе [Yamagi Quake II](https://github.com/yquake2/yquake2) и запускает данные оригинальной
игры на современных системах: любое разрешение, широкоформатные HUD и меню, рендерер OpenGL 3.2 с современными
эффектами и звук на OpenAL.

> **Проект в разработке.** Движок, графика, звук и меню работают; собственная игровая логика openMERC (режим B)
> загружает и запускает все 32 карты кампании, но ещё не доделана. Ошибки будут - см. [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

### Нужна своя копия игры

В openMERC **нет ни одного файла оригинальной игры**. Нужна своя лицензионная копия Soldier of Fortune (версия
GOG или диск с патчем 1.07f). openMERC читает `.pak`-файлы из вашей установки и ничего в неё не пишет.

### Состояние

- **Движок**: загружаются и рисуются все 75 стандартных карт; модели Ghoul, туман, ландшафт, небо, эффекты;
  сохранения и переходы между уровнями; консоль доступна всегда.
- **Графика**: по умолчанию максимальное качество; bloom только на светящихся поверхностях, объёмный туман, мягкие
  тени от солнца, попиксельные динамические источники света, мягкие частицы и другие эффекты, у каждого свой
  переключатель в меню «Видео» (часть ещё дорабатывается).
- **Меню**: новое широкоформатное меню со свободными шрифтами (Play, JetBrains Mono), которые масштабируются под
  любое разрешение и поддерживают латиницу и кириллицу. Родительского замка нет, насилие всегда на максимуме.
- **Перемещение**: движение игрока Quake II с отличиями SoF.
- **Два режима**:
  - *режим A* - движок openMERC + оригинальные `gamex86.dll` / `player.dll` из вашей установки (сейчас самая
    полная кампания);
  - *режим B* - всё открытое (движок, рендерер, звук, игровой модуль и модуль игрока). Все 32 карты кампании
    загружаются и работают, но ИИ противников, часть скриптовых сцен и детали оружия ещё не доделаны.
- **Сетевая игра**: серверы и клиенты openMERC играют друг с другом. С оригинальными серверами, клиентами и демо
  1.07f openMERC **не совместим** (см. «Чистая комната»), режима совместимости не будет.
- **Платформы**: Windows (32-битный движок на 64-битной Windows 10/11). Скрипты сборки для Linux есть
  (`tools/build/build-linux.sh`, [docs/BUILD-linux.md](docs/BUILD-linux.md)), но это экспериментально.

### Сборка (Windows)

Нужны Visual Studio 2022 Build Tools (MSVC **x86**), CMake 3.20+, Ninja, vcpkg (classic) с `sdl3:x86-windows` и
`openal-soft:x86-windows`, Python 3.10+. Укажите `VCVARSALL` и `OM_VCPKG` (корень vcpkg), при желании
`OM_BUILD_DIR`. Из корня репозитория в `cmd.exe`:

```
tools\build\build-msvc.cmd x86 Release
modules\sound\build.cmd Release
game\build.cmd Release
player\build.cmd Release
```

Соберите папку игры: файлы движка и звуковой модуль рядом, `gamex86.dll`, `player.dll` и `zz_openmerc.pak` в
`base\`, сверху `dist\openMERC.cmd`. Запустите `openMERC.cmd`; при первом запуске он спросит папку Soldier of
Fortune (ту, где лежит `base\pak0.pak`). Настройки, сохранения, скриншоты и логи - в `user\` рядом с запускалкой.
Для режима A удалите `gamex86.dll` и `player.dll` из `base\` openMERC.

### Чистая комната

См. [CLEANROOM.md](CLEANROOM.md) и [docs/LEGAL_NOTES.md](docs/LEGAL_NOTES.md). Код написан по спецификациям
поведения, исходникам Quake II под GPL и открытой документации; кода оригинальных исполняемых файлов в проекте нет.
Таблиц данных, скопированных из оригинальных исполняемых файлов, тоже нет, и движок никогда не читает `SoF.exe`.
Используются только данные игры (`*.pak`) из вашей лицензионной копии.

### Лицензия

GPL v2 или новее, см. [LICENSE](LICENSE). Сторонние компоненты и их лицензии - в [THIRD_PARTY.md](THIRD_PARTY.md).
Soldier of Fortune - товарный знак его владельцев; openMERC не связан с Raven Software или Activision.
