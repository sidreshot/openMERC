openMERC @VERSION@ for Windows
==========================

English | Русский ниже

openMERC is a free, open-source (GPL v2) engine for Soldier of Fortune (Raven Software, 2000):
modern resolutions and widescreen, an OpenGL 3.2 renderer with modern effects, OpenAL sound, a new menu.
It contains no files of the original game. You need your own copy of Soldier of Fortune.

INSTALL
1. Unpack the archive into your Soldier of Fortune folder (the folder that contains SoF.exe and base\).
   This adds openMerc.exe, a few DLLs and the folder "openmerc". No file of the game is replaced
   or changed. (Unpacking into a new folder inside the Soldier of Fortune folder works as well.)
2. Start openMerc.exe.

The game data (base\*.pak) is read from the Soldier of Fortune folder and never written.
openMERC's own files are in openmerc\. Settings, saves, screenshots (F11) and logs go to
openmerc\user\. Your original SoF saves and config (user\) stay untouched; the original SoF.exe keeps
working as before.
To uninstall, delete the openmerc folder and openMerc.exe, openMerc-ded.exe, ref_gl3.dll, ref_gles3.dll,
SDL3.dll, OMsnd.dll, OpenAL32.dll, fmt.dll, msvcp140.dll, msvcp140_atomic_wait.dll, vcruntime140.dll, vcomp140.dll,
omlight.exe, openMERC-README.txt.

Upgrading from 0.2.1 or 0.2.2: unpack @VERSION@ into the same folder and let it replace the openMERC files.
Settings and saves in openmerc\user\ are kept.
Upgrading from 0.2.0: 0.2.0 lived in its own folder openMERC\ (openMERC\openMerc.exe). Delete that
folder before unpacking @VERSION@ into the Soldier of Fortune folder (Windows treats "openMERC" and
"openmerc" as the same folder). Copy saves you want to keep from openMERC\user\save\ to
openmerc\user\save\ first.

NO GAME DATA FOUND?
If neither the folder of openMerc.exe nor its parent folder contains base\pak0.pak, openMERC asks for
the disc image (.iso) of your Soldier of Fortune copy and unpacks the game's .pak files from it into
base\ next to openMerc.exe (nothing but .pak files is taken from the image).

GRAPHICS
The Video menu has a Quality Preset (Minimal, Low, Medium, High or Custom); on integrated graphics a lighter
preset is chosen by itself at the first start. Every effect has its own switch in the Video menu.
The first time a map is loaded, omlight.exe re-bakes its lighting in the background (about half a minute;
openmerc\user\lightmaps\). The new light and the highlights of the surfaces are used from the next load of that map.
HD texture packs: put the pack's .pak into openmerc\hd\ and switch Video -> Picture Quality -> HD Textures
on (see openmerc\hd\README.txt).

SAVES
Saves of the original game (and of mode A) cannot be loaded with openMERC's own game modules, and
the other way round. The load menu shows such saves greyed out as "Not compatible".

SUPPORTED GAME VERSIONS
- Soldier of Fortune 1.07f: retail CD updated with the 1.07f patch (tested), and the GOG release
  (expected to work, not yet tested by us).
- Windows 10 or 11, 64-bit (openMERC itself is a 32-bit program). A GPU with OpenGL 3.2.
- If the game is installed under "C:\Program Files", Windows may block writing to openmerc\user.
  Install the game elsewhere (for example C:\Games) or run from a copy.

MODES
- Default: everything open source (openMERC engine + openMERC game and player modules in
  openmerc\). All 32 campaign maps load and run; enemy behaviour and some scripted scenes are unfinished.
- Original game logic ("mode A"): rename openmerc\gamex86.dll and openmerc\player.dll
  (for example to gamex86.dll.off and player.dll.off). openMERC then uses the gamex86.dll and player.dll
  of your SoF 1.07f installation (base\). Rename them back to return to the default.

MULTIPLAYER: openMERC plays with openMERC only. Original 1.07f servers and clients are not compatible.

Source code: https://github.com/sidreshot/openMERC (commit @COMMIT@), see openmerc\SOURCE.txt.
License: GPL v2 or later (openmerc\LICENSE.txt). Third-party components: openmerc\THIRD_PARTY.txt and
openmerc\licenses\.
Soldier of Fortune is a trademark of its owners. openMERC is not affiliated with Raven Software or Activision.


Русский
=======

openMERC — бесплатный движок с открытым кодом (GPL v2) для Soldier of Fortune (Raven Software, 2000):
современные разрешения и широкий экран, рендер OpenGL 3.2 с современными эффектами, звук OpenAL, новое меню.
Файлов оригинальной игры в нём нет. Нужна ваша собственная копия Soldier of Fortune.

УСТАНОВКА
1. Распакуйте архив в папку Soldier of Fortune (ту, где лежат SoF.exe и base\).
   Добавятся openMerc.exe, несколько DLL и папка «openmerc». Ни один файл игры не заменяется и не
   меняется. (Можно распаковать и в новую папку внутри папки Soldier of Fortune.)
2. Запустите openMerc.exe.

Данные игры (base\*.pak) читаются из папки Soldier of Fortune, запись туда не ведётся.
Собственные файлы openMERC лежат в openmerc\. Настройки, сохранения, скриншоты (F11) и логи — в
openmerc\user\. Ваши оригинальные сохранения и конфиг SoF (user\) не трогаются, оригинальный SoF.exe
работает как раньше.
Чтобы удалить openMERC, удалите папку openmerc и файлы openMerc.exe, openMerc-ded.exe, ref_gl3.dll,
ref_gles3.dll, SDL3.dll, OMsnd.dll, OpenAL32.dll, fmt.dll, msvcp140.dll, msvcp140_atomic_wait.dll,
vcruntime140.dll, vcomp140.dll, omlight.exe, openMERC-README.txt.

Обновление с 0.2.1 или 0.2.2: распакуйте @VERSION@ в ту же папку и разрешите заменить файлы openMERC.
Настройки и сохранения в openmerc\user\ остаются.
Обновление с 0.2.0: версия 0.2.0 жила в отдельной папке openMERC\ (openMERC\openMerc.exe). Удалите эту
папку перед распаковкой @VERSION@ в папку Soldier of Fortune (для Windows «openMERC» и «openmerc» — одна и
та же папка). Нужные сохранения сначала скопируйте из openMERC\user\save\ в openmerc\user\save\.

НЕТ ДАННЫХ ИГРЫ?
Если ни в папке openMerc.exe, ни в папке выше нет base\pak0.pak, openMERC попросит образ диска (.iso)
вашей копии Soldier of Fortune и распакует из него .pak-файлы игры в base\ рядом с openMerc.exe
(из образа берутся только .pak-файлы).

ГРАФИКА
В меню «Видео» есть «Пресет качества» (Минимальные, Низкие, Средние, Высокие или Свои); на встроенной
графике при первом запуске подходящий пресет выбирается сам. У каждого эффекта свой переключатель в меню «Видео».
При первой загрузке карты omlight.exe в фоне пересчитывает её освещение (около полуминуты;
openmerc\user\lightmaps\). Новый свет и блики на поверхностях появляются со следующей загрузки этой карты.
HD-текстуры: положите .pak HD-пака в openmerc\hd\ и включите Видео -> Качество изображения -> HD-текстуры
(см. openmerc\hd\README.txt).

СОХРАНЕНИЯ
Сохранения оригинальной игры (и режима A) нельзя загрузить с собственными модулями игры openMERC, и
наоборот. В меню загрузки такие сохранения показаны серыми с пометкой «Несовместимо».

ПОДДЕРЖИВАЕМЫЕ ВЕРСИИ ИГРЫ
- Soldier of Fortune 1.07f: диск с патчем 1.07f (проверено) и версия GOG (должна работать,
  нами пока не проверена).
- Windows 10 или 11, 64-бит (сам openMERC — 32-битная программа). Видеокарта с OpenGL 3.2.
- Если игра установлена в «C:\Program Files», Windows может не дать писать в openmerc\user.
  Установите игру в другое место (например, C:\Games).

РЕЖИМЫ
- По умолчанию: всё открытое (движок openMERC + модули игры и игрока openMERC в openmerc\).
  Все 32 карты кампании загружаются и работают; поведение противников и часть скриптовых сцен ещё не доделаны.
- Оригинальная игровая логика («режим A»): переименуйте openmerc\gamex86.dll и openmerc\player.dll
  (например, в gamex86.dll.off и player.dll.off). Тогда openMERC возьмёт gamex86.dll и player.dll
  из вашей SoF 1.07f (base\). Чтобы вернуть режим по умолчанию, переименуйте обратно.

СЕТЕВАЯ ИГРА: openMERC играет только с openMERC. Оригинальные серверы и клиенты 1.07f несовместимы.

Исходный код: https://github.com/sidreshot/openMERC (коммит @COMMIT@), см. openmerc\SOURCE.txt.
Лицензия: GPL v2 или новее (openmerc\LICENSE.txt). Сторонние компоненты: openmerc\THIRD_PARTY.txt и
папка openmerc\licenses\.
Soldier of Fortune — товарный знак его владельцев. openMERC не связан с Raven Software и Activision.
