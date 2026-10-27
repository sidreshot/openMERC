# OpenMerc MENU2: writes OpenMerc's own string packages (data/base/strip) from
# the tables below (UTF-8 here, CP1251 bytes in the packages, the code page of
# the SoF fonts). TEXT_RUSSIAN is an OpenMerc extension (sof_strings.c): it is
# taken while Russian is active (om_language, auto-detected on Russian installs).
# Package IDs pick slots no 1.07f package uses (196-199, 201; menu_generic is 200):
# a package registered into a used slot hides the other one.
# Run: python tools/menu2/gen_strings.py
import os, re

ROOT = os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'base', 'strip')

def parse_existing(path):
    """reference -> {lang: text} of an existing OpenMerc package (DE/FR kept)"""
    out = {}
    if not os.path.exists(path):
        return out
    cur = None
    for line in open(path, encoding='cp1251').read().splitlines():
        s = line.strip()
        m = re.match(r'REFERENCE (\w+)', s)
        if m and line.startswith(' '):
            cur = m.group(1); out[cur] = {}; continue
        m = re.match(r'(TEXT_\w+) "(.*)"$', s)
        if m and cur:
            out[cur][m.group(1)] = m.group(2)
    return out

def write(name, pid, ref, desc, entries):
    # a package holds INDEX 0..255 (SP_MAX_ENTRIES): later entries were dropped silently
    assert len(entries) <= 256, '%s: %d entries, at most 256' % (name, len(entries))
    lines = ['VERSION 1', 'ID %d' % pid, 'REFERENCE %s' % ref, 'DESCRIPTION "%s"' % desc,
             'COUNT %d' % len(entries)]
    for i, (r, texts) in enumerate(entries):
        lines += ['INDEX %d' % i, '{', '   REFERENCE %s' % r]
        for lang in ('TEXT_ENGLISH', 'TEXT_GERMAN', 'TEXT_FRENCH', 'TEXT_RUSSIAN'):
            if lang in texts:
                lines.append('   %s "%s"' % (lang, texts[lang]))
        lines.append('}')
    # Ё/ё are written as they are: the TrueType menu fonts have them and the bitmap
    # fallback draws Е/е (m_draw.c MR_DrawString)
    data = ('\r\n'.join(lines) + '\r\n').encode('cp1251')
    with open(os.path.join(ROOT, name + '.sp'), 'wb') as f:
        f.write(data)
    print('%s: %d entries, %d bytes' % (name, len(entries), len(data)))

def er(table):
    return [(r, {'TEXT_ENGLISH': en, 'TEXT_RUSSIAN': ru}) for r, en, ru in table]

# ---- menu_omvideo (V1's page, ID 199): Russian added, UI size row added
VIDEO_RU = {
    'MODES': 'Рабочий стол,1280x720,1366x768,1600x900,1920x1080,1920x1200,2560x1080,2560x1440,2560x1600,3440x1440,3840x2160,1024x768,1280x960,1600x1200',
    'DISPLAY': 'В окне,Полный экран,Без рамки',
    'DISPLAY_LABEL': 'Режим экрана',
    'VSYNC': 'Вертикальная синхронизация',
    'FPS': 'Частота экрана,60,120,144,165,240,Без ограничения',
    'FPS_LABEL': 'Ограничение кадров',
    'FOV': '75,80,85,90,95,100,105,110',
    'FOV_LABEL': 'Поле зрения',
    'ASPECT': 'Широкоэкранные HUD и меню',
    'AF': 'Выкл,2x,4x,8x,16x',
    'AF_LABEL': 'Анизотропная фильтрация',
    'MSAA': 'Выкл,2x,4x,8x',
    'MSAA_LABEL': 'Сглаживание (MSAA)',
    'RESTART_NOTE': 'Изменения выше вступают в силу после применения',
    'HUDSIZE': 'Авто,1x,2x,3x,4x',
    'HUDSIZE_LABEL': 'Размер HUD',
    'MODES_LABEL': 'Разрешение',
    'OFFON': 'Выкл,Вкл',
    'SECTION_DISPLAY': 'ЭКРАН',
    'SECTION_PICTURE': 'КАЧЕСТВО ИЗОБРАЖЕНИЯ',
    'SECTION_COLOUR': 'ЦВЕТ',
    'APPLY_TIP': 'Настройки экрана применяются кнопкой «Применить»',
    'TEXMODE': 'Билинейная,Трилинейная,Линейная без MIP-уровней,Без фильтрации',
    'TEXMODE_LABEL': 'Фильтрация текстур',
    'DETAIL_LABEL': 'Детальные текстуры',
    'DETAILDIST': 'Близко,Средне,Далеко,Очень далеко,Максимум',
    'DETAILDIST_LABEL': 'Дальность детальных текстур',
    'LIGHTMETHOD': 'Быстрейшее,Быстрое,Качественное,Наилучшее',
    'LIGHTMETHOD_LABEL': 'Освещение персонажей',
    'SPECULAR_LABEL': 'Блики',
    'GAMMA': 'Гамма',
    'CONTRAST': 'Контраст',
    'BRIGHTNESS': 'Яркость',
    'LIGHTINTENSITY': 'Интенсивность света',
    'APPLY': 'ПРИМЕНИТЬ',
    'DEFAULTS': 'ПО УМОЛЧАНИЮ',
    'UISCALE': 'Авто,1x,2x,3x,4x',
    'UISCALE_LABEL': 'Размер меню и текста',
    'DESKTOP': 'Как на рабочем столе',
    'SECTION_SCALING': 'МАСШТАБ',
    'SECTION_VIEW': 'ОБЗОР',
    'HORPLUS_LABEL': 'Широкий обзор (Hor+)',
}
VIDEO_NEW = {
    'DESKTOP': {'TEXT_ENGLISH': 'Desktop', 'TEXT_GERMAN': 'Desktop', 'TEXT_FRENCH': 'Bureau'},
    'SECTION_SCALING': {'TEXT_ENGLISH': 'SCALING', 'TEXT_GERMAN': 'SKALIERUNG', 'TEXT_FRENCH': 'ECHELLE'},
    'SECTION_VIEW': {'TEXT_ENGLISH': 'VIEW', 'TEXT_GERMAN': 'SICHT', 'TEXT_FRENCH': 'VUE'},
    'HORPLUS_LABEL': {'TEXT_ENGLISH': 'Widescreen View (Hor+)', 'TEXT_GERMAN': 'Breitbildsicht (Hor+)', 'TEXT_FRENCH': 'Vue panoramique (Hor+)'},
    'UISCALE': {'TEXT_ENGLISH': 'Auto,1x,2x,3x,4x', 'TEXT_GERMAN': 'Auto,1x,2x,3x,4x', 'TEXT_FRENCH': 'Auto,1x,2x,3x,4x'},
    'UISCALE_LABEL': {'TEXT_ENGLISH': 'Menu and Text Size', 'TEXT_GERMAN': 'Menue- und Textgroesse', 'TEXT_FRENCH': 'Taille des menus et du texte'},
}

def video():
    path = os.path.join(ROOT, 'menu_omvideo.sp')
    old = parse_existing(path)
    for k, v in VIDEO_NEW.items():
        old.setdefault(k, v)
    # HUD size up to 4x (2160p and above)
    old['HUDSIZE'] = {k: 'Auto,1x,2x,3x,4x' for k in ('TEXT_ENGLISH', 'TEXT_GERMAN', 'TEXT_FRENCH')}
    entries = []
    for r, texts in old.items():
        t = {k: v for k, v in texts.items() if k != 'TEXT_RUSSIAN'}
        t['TEXT_RUSSIAN'] = VIDEO_RU[r]
        entries.append((r, t))
    write('menu_omvideo', 199, 'MENU_OMVIDEO', 'OpenMerc video page', entries)

# ---- menu_omset (ID 197): audio, controls, gameplay and multiplayer pages
SET = [
    ('OFFON', 'Off,On', 'Выкл,Вкл'),
    ('NOYES', 'No,Yes', 'Нет,Да'),
    ('SECTION_VOLUME', 'VOLUME', 'ГРОМКОСТЬ'),
    ('MASTER', 'Master Volume', 'Общая громкость'),
    ('EFFECTS', 'Effects Volume', 'Громкость эффектов'),
    ('VOICE', 'Speech Volume', 'Громкость речи'),
    ('MUSIC', 'Music Volume', 'Громкость музыки'),
    ('MUSIC_ON', 'Music', 'Музыка'),
    ('SECTION_OUTPUT', 'SOUND OUTPUT', 'ВЫВОД ЗВУКА'),
    ('HRTF', 'Headphone 3D Sound (HRTF)', 'Объёмный звук в наушниках (HRTF)'),
    ('REVERB', 'Environmental Reverb', 'Реверберация окружения'),
    ('LIMITER', 'Volume Limiter', 'Ограничитель громкости'),
    ('FOCUSMUTE', 'Mute in Background', 'Без звука в фоне'),
    ('SND_RESTART', 'RESTART SOUND', 'ПЕРЕЗАПУСК ЗВУКА'),
    ('SND_DEFAULTS', 'RESTORE DEFAULTS', 'ПО УМОЛЧАНИЮ'),
    ('SND_TIP', 'Output settings apply at once', 'Настройки вывода применяются сразу'),
    ('SND_RESTART_TIP', 'Reloads every sound', 'Заново загружает все звуки'),
    ('DEVICE', 'Device', 'Устройство'),
    ('DEVICE_DEFAULT', 'System Default', 'Системное по умолчанию'),
    ('DEVICE_TIP', 'System Default follows Windows', 'Системное следует настройке Windows'),
    ('SECTION_MOUSE', 'MOUSE', 'МЫШЬ'),
    ('SENS_X', 'Horizontal Sensitivity', 'Чувствительность по горизонтали'),
    ('SENS_Y', 'Vertical Sensitivity', 'Чувствительность по вертикали'),
    ('INVERT_X', 'Invert Horizontal', 'Инверсия по горизонтали'),
    ('INVERT_Y', 'Invert Vertical', 'Инверсия по вертикали'),
    ('FREELOOK', 'Mouse Look', 'Обзор мышью'),
    ('FILTER', 'Mouse Smoothing', 'Сглаживание мыши'),
    ('SECTION_MOVE', 'MOVEMENT', 'ПЕРЕДВИЖЕНИЕ'),
    ('RUN', 'Always Run', 'Всегда бежать'),
    ('LOOKSPRING', 'Centre View on Release', 'Центрировать взгляд'),
    ('SECTION_PAD', 'GAMEPAD', 'ГЕЙМПАД'),
    ('PAD_SENS', 'Stick Look Speed', 'Скорость обзора стиком'),
    ('KEYS', 'KEY BINDINGS', 'НАЗНАЧЕНИЕ КЛАВИШ'),
    ('KEY_PRESETS', 'KEY LAYOUTS', 'ГОТОВЫЕ РАСКЛАДКИ'),
    ('KEY_MOVE', 'MOVING', 'ДВИЖЕНИЕ'),
    ('KEY_COMBAT', 'COMBAT', 'БОЙ'),
    ('KEY_SLOTS', 'WEAPON SLOTS', 'ОРУЖИЕ ПО НОМЕРАМ'),
    ('KEY_ITEMS', 'ITEMS', 'ПРЕДМЕТЫ'),
    ('KEY_UI', 'MULTIPLAYER AND INTERFACE', 'СЕТЬ И ИНТЕРФЕЙС'),
    ('KEY_LOOK', 'LOOKING', 'ОБЗОР'),
    ('KEY_LASTWEAP', 'Last Weapon', 'Предыдущее оружие'),
    ('KEY_CONSOLE', 'Console', 'Консоль'),
    ('KEY_LEANTIP', 'Hold Use and press Step Left or Step Right to lean', 'Удерживайте клавишу действия и нажмите шаг влево или вправо, чтобы выглянуть'),
    ('KEY_BINDTIP', 'Click an action, then press the new key', 'Щёлкните действие и нажмите новую клавишу'),
    ('SECTION_VIEW', 'VIEW', 'ОБЗОР'),
    ('FOV_LABEL', 'Field of View', 'Поле зрения'),
    ('FOV', '75,80,85,90,95,100,105,110', '75,80,85,90,95,100,105,110'),
    ('CROSSHAIR', 'Crosshair', 'Прицел'),
    ('CROSSHAIRS', 'Off,Style 1,Style 2,Style 3,Style 4', 'Выкл,Вид 1,Вид 2,Вид 3,Вид 4'),
    ('CH_SIZE', 'Crosshair Size', 'Размер прицела'),
    ('CH_SIZES', 'Auto,1x,2x,3x,4x', 'Авто,1x,2x,3x,4x'),
    ('SUBTITLES', 'Subtitles', 'Субтитры'),
    ('LANGUAGE', 'Text Language', 'Язык текста'),
    ('LANGUAGES', 'Auto,English,Russian', 'Авто,Английский,Русский'),
    ('SECTION_WORLD', 'WORLD', 'МИР'),
    ('CORPSES', 'Bodies Kept', 'Тела на уровне'),
    ('CORPSE_OPTS', '2,5,10,20', '2,5,10,20'),
    ('SECTION_NET', 'MULTIPLAYER', 'СЕТЕВАЯ ИГРА'),
    ('TEAM', 'Team', 'Команда'),
    ('TEAMS', 'Blue,Red', 'Синие,Красные'),
    ('SPECTATOR', 'Spectator Mode', 'Режим зрителя'),
    ('NAMES', 'Show Player Names', 'Показывать имена игроков'),
    ('WEAPSWITCH', 'Switch to New Weapon', 'Брать новое оружие'),
    ('WEAPSWITCHES', 'Never,Safe,Always', 'Никогда,Безопасно,Всегда'),
    ('RATE', 'Connection', 'Соединение'),
    ('RATES', 'Slow,Medium,Fast', 'Медленное,Среднее,Быстрое'),
    ('SECTION_DL', 'DOWNLOADS', 'ЗАГРУЗКИ'),
    ('DL', 'Allow Downloads', 'Разрешить загрузки'),
    ('DL_MAPS', 'Maps', 'Карты'),
    ('DL_SOUNDS', 'Sounds', 'Звуки'),
    ('DL_STRINGS', 'Text Packages', 'Текстовые пакеты'),
    # MENU-SCRATCH navigation rail (menus/om_rail.rmf)
    ('RAIL_MAIN', 'Main Menu', 'Главное меню'),
    ('RAIL_SINGLE', 'CAMPAIGN', 'КАМПАНИЯ'),
    ('RAIL_NEWGAME', 'New Game', 'Новая игра'),
    ('RAIL_LOAD', 'Load Game', 'Загрузить'),
    ('RAIL_SAVE', 'Save Game', 'Сохранить'),
    ('RAIL_CREDITS', 'Credits', 'Авторы'),
    ('RAIL_MULTI', 'MULTIPLAYER', 'СЕТЕВАЯ ИГРА'),
    ('RAIL_PLAYER', 'Player Setup', 'Игрок'),
    ('RAIL_JOIN', 'Join Server', 'Подключиться'),
    ('RAIL_START', 'Start Server', 'Создать сервер'),
    ('RAIL_MPOPTIONS', 'Server Options', 'Параметры сервера'),
    ('RAIL_OPTIONS', 'OPTIONS', 'НАСТРОЙКИ'),
    ('RAIL_KEYS', 'Controls', 'Управление'),
    ('RAIL_VIDEO', 'Video', 'Видео'),
    ('RAIL_AUDIO', 'Audio', 'Звук'),
    ('RAIL_GAMEPLAY', 'Gameplay', 'Игра'),
    ('RAIL_MOUSE', 'Mouse and Gamepad', 'Мышь и геймпад'),
    ('RAIL_MORE', 'OTHER', 'ПРОЧЕЕ'),
    ('RAIL_STATS', 'Statistics', 'Статистика'),
    ('RAIL_QUIT', 'Quit', 'Выход'),
    # wide layout (menus/m_wide.c): tab and footer labels
    ('TAB_PLAYER', 'Player', 'Игрок'),
    ('TAB_JOIN', 'Join', 'Подключиться'),
    ('TAB_START', 'Start', 'Создать'),
    ('TAB_OPTIONS', 'Server', 'Сервер'),
    ('TAB_MOTD', 'News', 'Новости'),
    ('TAB_CHAT', 'Chat', 'Чат'),
    ('TAB_GAMEPAD', 'Gamepad', 'Геймпад'),
    ('TAB_MOUSE', 'Mouse', 'Мышь'),
    ('TAB_LOCK', 'Lock', 'Замок'),
    ('TAB_RULES', 'Rules', 'Правила'),
    ('TAB_BOTS', 'Bots', 'Боты'),
    ('TAB_LANGUAGE', 'Language', 'Язык'),
    ('WIDE_HELP', 'Help', 'Справка'),
    ('WIDE_RESUME', 'Resume', 'В игру'),
    ('WIDE_BACK', 'Back', 'Назад'),
    ('WIDE_DEPLOY', 'Deploy', 'В бой'),
    # server browser status line (cl_serverlist.c, menus/m_data.c)
    ('SL_TRANSFER', 'Receiving Server List', 'Получение списка серверов'),
    ('SL_QUERY', 'Querying Servers', 'Опрос серверов'),
    ('SL_DONE', 'Query Complete', 'Опрос завершен'),
    # main menu: rail + landing page (menus/main.rmf, t_omhome.rmf, m_omhome.rmf)
    ('HOME_TAB', 'Home', 'Главная'),
    ('HOME_TITLE', 'MAIN MENU', 'ГЛАВНОЕ МЕНЮ'),
    ('HOME_TICKER', 'openMERC  -  an open-source engine for Soldier of Fortune  -  your game data stays yours', 'openMERC  -  открытый движок для Soldier of Fortune  -  нужны файлы вашей копии игры'),
    ('HOME_CONTINUE', 'CONTINUE', 'ПРОДОЛЖИТЬ'),
    ('HOME_NEWGAME', 'NEW GAME', 'НОВАЯ ИГРА'),
    ('HOME_LOAD', 'LOAD GAME', 'ЗАГРУЗИТЬ ИГРУ'),
    ('HOME_MULTI', 'MULTIPLAYER', 'СЕТЕВАЯ ИГРА'),
    ('HOME_SETTINGS', 'SETTINGS', 'НАСТРОЙКИ'),
    ('HOME_QUIT', 'EXIT', 'ВЫХОД'),
    ('HOME_LASTSAVE', 'LAST SAVE', 'ПОСЛЕДНЕЕ СОХРАНЕНИЕ'),
    ('HOME_NOSAVE', 'No saved games yet', 'Сохранённых игр пока нет'),
    # gameplay page: timed autosave (cvar om_autosave_interval, minutes, 0 = off)
    ('SECTION_SAVE', 'SAVING', 'СОХРАНЕНИЕ'),
    ('AUTOSAVE', 'Timed Autosave', 'Автосохранение по таймеру'),
    ('AUTOSAVE_OPTS', 'Off,1 min,2 min,3 min,4 min,5 min', 'Выкл,1 мин,2 мин,3 мин,4 мин,5 мин'),
]

# ---- menu_omfill (ID 198): texts the stock pages reference but no stock
# package of the install defines (om_rmftest "Could not find token");
# menus/m_strings.c looks up OMFILL_<reference> when a reference is missing
NA_EN = 'This game mode is not part of this version.'
NA_RU = 'Этот режим игры не входит в данную версию.'
FILL = [
    ('BOT_MENU_ADD', 'Add', 'Добавить'),
    ('BOT_MENU_ADD_BOT', 'Add Bot', 'Добавить бота'),
    ('BOT_MENU_APPLY', 'Apply', 'Применить'),
    ('BOT_MENU_BOT_DETAILS', 'Bot Details', 'Сведения о боте'),
    ('BOT_MENU_BOT_SETTINGS', 'Bot Settings', 'Настройки ботов'),
    ('BOT_MENU_CONFIGURE_BOTS', 'Configure Bots', 'Настройка ботов'),
    ('BOT_MENU_PICK_A_BOT', 'Pick a Bot', 'Выбор бота'),
    ('BOT_MENU_REMOVE_BOT', 'Remove Bot', 'Убрать бота'),
    ('BOT_MENU_TICKER_ALTER_BOT', 'Change the bots in the match', 'Измените состав ботов в матче'),
    ('BOT_MENU_TICKER_PICK_A_BOT', 'Choose a bot for the match', 'Выберите бота для матча'),
    ('MENU_CREDITS_HGUERRERO', ' ', ' '),
    ('MENU_DM_GENERIC_CONQUER_TITLE', 'Conquer', 'Захват'),
    ('MENU_DM_GENERIC_CONQUER_DESCR', NA_EN, NA_RU),
    ('MENU_DM_GENERIC_CONTROL_TITLE', 'Control', 'Контроль'),
    ('MENU_DM_GENERIC_CONTROL_DESCR', NA_EN, NA_RU),
    ('MENU_DM_GENERIC_CTB_TITLE', 'CTB', 'CTB'),
    ('MENU_DM_GENERIC_CTB_DESCR', NA_EN, NA_RU),
    ('MENU_DM_GENERIC_CONTROL_LIMIT', 'Control Limit', 'Лимит контроля'),
    ('MENU_DM_GENERIC_DELAY', 'Delay', 'Задержка'),
    ('MENU_DM_GENERIC_DM6', 'Control', 'Контроль'),
    ('MENU_DM_GENERIC_DM7', 'Conquer', 'Захват'),
    # server list column header: the column is 7 characters wide
    ('MENU_DM_GENERIC_PURE', 'Pure', 'Чист.'),
    ('MENU_DM_GENERIC_TEAM_REDBLUE', 'Red and Blue Teams', 'Красные и синие'),
    ('MENU_DM_GENERIC_TEAM_REDBLUE_QUES', 'Play with red and blue teams?', 'Играть красной и синей командами?'),
    ('MENU_DOWNLOAD_BLUE_RED', 'Blue,Red', 'Синие,Красные'),
    ('MENU_DOWNLOAD_TEAM', 'Team', 'Команда'),
    ('MENU_IRQ3A_RECOMMENDED', 'Recommended weapons for this map', 'Рекомендуемое оружие для этой карты'),
    ('MENU_SUD3_RECOMMENDED', 'Recommended weapons for this map', 'Рекомендуемое оружие для этой карты'),
    ('MENU_KOSDM2_INFO', 'Deathmatch map.', 'Карта для боя насмерть.'),
    ('MENU_PLAYER_NAMES_HARD', ' ', ' '),
]
for who, en, ru in (('ASSAULT', 'Assault', 'Штурмовик'), ('BRICK', 'Brick', 'Брик'), ('COBRA', 'Cobra', 'Кобра'),
                    ('COMMANDO', 'Commando', 'Коммандос'), ('JERSEY', 'Jersey', 'Джерси'),
                    ('WIDOWMAKER', 'Widowmaker', 'Вдоводел')):
    FILL += [('MENU_PLAYER_NAMES_NAME_' + who, en, ru), ('MENU_PLAYER_NAMES_AKA_' + who, en, ru),
             ('MENU_PLAYER_NAMES_INFO_' + who, 'No dossier available.', 'Досье отсутствует.')]

# ---- menu_omovr (ID 196): OpenMerc's own wording for stock references whose
# stock text no longer fits the rewritten pages (tab tips, page titles, tickers);
# menus/m_strings.c looks up OMOVR_<reference> before the stock text
OVR = [
    # 1.07f bot.sp has this button in English only, in capitals
    ('BOT_MENU_BOT_SETTINGS', 'BOT SETTINGS', 'НАСТРОЙКИ БОТОВ'),
    # 1.07f menu_dm_generic.sp has no Russian for the server list's Pure column
    ('MENU_DM_GENERIC_PURE_YES', 'Yes', 'Да'),
    ('MENU_DM_GENERIC_PURE_NO', 'No', 'Нет'),
    ('MENU_GENERIC_ALTER_KEYS', 'Controls: key bindings and presets', 'Управление: привязки клавиш и раскладки'),
    ('MENU_GENERIC_ALTER_DISPLAY', 'Video: screen, scaling, picture quality', 'Видео: экран, масштаб, качество изображения'),
    ('MENU_GENERIC_ALTER_AUDIO', 'Audio: volume and sound output', 'Звук: громкость и вывод звука'),
    ('MENU_GENERIC_ADJUST_DYNAMIC', 'Gameplay: view, world, saving', 'Игра: обзор, мир, сохранение'),
    ('MENU_GENERIC_MISC', 'Mouse, movement and gamepad', 'Мышь, передвижение и геймпад'),
    ('MENU_SCALE_TITLE', 'GAMEPLAY', 'ИГРОВОЙ ПРОЦЕСС'),
    ('MENU_SCALE_TICKER', '*** VIEW, WORLD AND SAVING SETTINGS ***', '*** НАСТРОЙКИ ОБЗОРА, МИРА И СОХРАНЕНИЯ ***'),
    ('MENU_KEYS_TITLE2', 'MOUSE AND MOVEMENT', 'МЫШЬ И ПЕРЕДВИЖЕНИЕ'),
    ('MENU_KEYS_MISC_TICKER', '*** MOUSE, MOVEMENT AND GAMEPAD ***', '*** МЫШЬ, ПЕРЕДВИЖЕНИЕ И ГЕЙМПАД ***'),
    ('MENU_AUDIO_TICKER', "*** OUTPUT SETTINGS APPLY AT ONCE *** 'SOUND RESTART' RELOADS EVERY SOUND ***",
     "*** НАСТРОЙКИ ВЫВОДА ПРИМЕНЯЮТСЯ СРАЗУ *** 'ПЕРЕЗАПУСК ЗВУКА' ЗАНОВО ЗАГРУЖАЕТ ВСЕ ЗВУКИ ***"),
    ('MENU_VIDEO_TICKER', "*** CLICK AN OPTION TO CHANGE IT *** SCREEN SETTINGS TAKE EFFECT WITH 'APPLY' ***",
     "*** ЩЕЛКНИТЕ ПО ПАРАМЕТРУ, ЧТОБЫ ИЗМЕНИТЬ ЕГО *** НАСТРОЙКИ ЭКРАНА ВСТУПАЮТ В СИЛУ КНОПКОЙ 'ПРИМЕНИТЬ' ***"),
]

# ---- own pages of the folder screens (menus/*.rmf, MENU-SCRATCH owner t95u/t104u):
# titles, tickers, dialogs, key actions, server-list rules (prefix PG_, package menu_omset)
PG = [
    # footer buttons (control.rmf)
    ('PG_HOME_TIP', 'Back to the main menu', 'В главное меню'),
    ('PG_RESUME_TIP', 'Back to the game', 'Вернуться в игру'),
    ('PG_BACK_TIP', 'Previous page', 'Предыдущая страница'),
    # campaign folder
    ('PG_GAME_TITLE', 'NEW GAME', 'НОВАЯ ИГРА'),
    ('PG_GAME_TICK', '*** CHOOSE A DIFFICULTY TO START THE CAMPAIGN *** THE TUTORIAL TEACHES THE CONTROLS ***',
     '*** ВЫБЕРИТЕ СЛОЖНОСТЬ, ЧТОБЫ НАЧАТЬ КАМПАНИЮ *** ОБУЧЕНИЕ ЗНАКОМИТ С УПРАВЛЕНИЕМ ***'),
    ('PG_GAME_NAME', 'YOUR NAME', 'ВАШЕ ИМЯ'),
    ('PG_GAME_NAME_TIP', 'Shown in saved games and in multiplayer', 'Видно в сохранениях и в сетевой игре'),
    ('PG_GAME_DIFF', 'DIFFICULTY', 'СЛОЖНОСТЬ'),
    ('PG_GAME_D0', 'Very Easy', 'Очень легко'),
    ('PG_GAME_D1', 'Easy', 'Легко'),
    ('PG_GAME_D2', 'Normal', 'Нормально'),
    ('PG_GAME_D3', 'Hard', 'Сложно'),
    ('PG_GAME_D4', 'Very Hard', 'Очень сложно'),
    ('PG_GAME_DCUSTOM', 'Custom...', 'Своя настройка...'),
    ('PG_GAME_DTIP', 'Harder levels: fewer saves, more enemies, tougher enemies, less to carry',
     'Выше сложность: меньше сохранений, больше и сильнее враги, меньше снаряжения'),
    ('PG_GAME_TRAIN', 'TRAINING', 'ОБУЧЕНИЕ'),
    ('PG_GAME_TUTORIAL', 'Tutorial', 'Обучение'),
    ('PG_LOAD_TITLE', 'LOAD GAME', 'ЗАГРУЗКА ИГРЫ'),
    ('PG_LOAD_TICK', '*** CLICK A SAVED GAME TO LOAD IT *** RIGHT-CLICK TO DELETE IT ***',
     '*** ЩЕЛЧОК ПО СОХРАНЕНИЮ ЗАГРУЖАЕТ ЕГО *** ПРАВЫЙ ЩЕЛЧОК УДАЛЯЕТ ***'),
    ('PG_SAVE_TITLE', 'SAVE GAME', 'СОХРАНЕНИЕ ИГРЫ'),
    ('PG_SAVE_TICK', '*** NEW SAVE ADDS A SAVED GAME *** CLICK ONE TO OVERWRITE IT, RIGHT-CLICK TO DELETE IT ***',
     '*** «НОВОЕ СОХРАНЕНИЕ» ДОБАВЛЯЕТ ЗАПИСЬ *** ЩЕЛЧОК ПЕРЕЗАПИСЫВАЕТ, ПРАВЫЙ ЩЕЛЧОК УДАЛЯЕТ ***'),
    ('PG_SAVE_NEW', 'NEW SAVE', 'НОВОЕ СОХРАНЕНИЕ'),
    ('PG_SAVE_LEFT', 'Saves left', 'Осталось сохранений'),
    ('PG_CREDITS_TITLE', 'CREDITS', 'АВТОРЫ'),
    ('PG_CREDITS_TICK', '*** THE PEOPLE WHO MADE SOLDIER OF FORTUNE ***', '*** ЛЮДИ, СОЗДАВШИЕ SOLDIER OF FORTUNE ***'),
    # dialogs
    ('PG_SURE', 'Are you sure?', 'Вы уверены?'),
    ('PG_NO', 'No', 'Нет'),
    ('PG_YES', 'Yes', 'Да'),
    ('PG_QUIT_Q', 'Quit openMERC?', 'Выйти из openMERC?'),
    ('PG_OVERWRITE_Q', 'Overwrite this saved game?', 'Перезаписать это сохранение?'),
    ('PG_WIPE_Q', 'Delete this saved game?', 'Удалить это сохранение?'),
    ('PG_LOADING_PLAYERS', 'Loading player models...', 'Загрузка моделей игроков...'),
    # options folder
    ('PG_KEYS_TITLE', 'CONTROLS', 'УПРАВЛЕНИЕ'),
    ('PG_KEYS_TICK', '*** CLICK AN ACTION, THEN PRESS A KEY *** BACKSPACE CLEARS IT *** ESC CANCELS ***',
     '*** ЩЕЛКНИТЕ ДЕЙСТВИЕ И НАЖМИТЕ КЛАВИШУ *** BACKSPACE ОЧИЩАЕТ *** ESC ОТМЕНЯЕТ ***'),
    ('PG_VIDEO_TITLE', 'VIDEO', 'ВИДЕО'),
    ('PG_AUDIO_TITLE', 'AUDIO', 'ЗВУК'),
    ('PG_SCALE_TITLE', 'GAMEPLAY', 'ИГРОВОЙ ПРОЦЕСС'),
    ('PG_MISC_TITLE', 'MOUSE AND MOVEMENT', 'МЫШЬ И ПЕРЕДВИЖЕНИЕ'),
    ('PG_LANG_TITLE', 'LANGUAGE', 'ЯЗЫК'),
    ('PG_LANG_TICK', '*** TEXT LANGUAGE OF MENUS, MESSAGES AND SUBTITLES ***', '*** ЯЗЫК МЕНЮ, СООБЩЕНИЙ И СУБТИТРОВ ***'),
    ('PG_LANG_TEXT', 'Text Language', 'Язык текста'),
    ('PG_LANG_TEXTS', 'Auto,English,Русский', 'Авто,English,Русский'),
    ('PG_LANG_BASE', 'Game Text Set', 'Набор текстов игры'),
    ('PG_LANG_BASES', 'English (US),English (UK),Francais', 'English (US),English (UK),Francais'),
    ('PG_LANG_BASE_TIP', 'Used for every text with no Russian version; French needs a French install',
     'Для текстов без русского перевода; французский требует французскую версию игры'),
    ('PG_APPLY', 'APPLY', 'ПРИМЕНИТЬ'),
    ('PG_KEYS_SAVE', 'SAVE BINDINGS', 'СОХРАНИТЬ'),
    ('PG_KEYS_DEFAULTS', 'DEFAULT BINDINGS', 'ПО УМОЛЧАНИЮ'),
    ('PG_KEYS_UNBIND', 'CLEAR ALL', 'ОЧИСТИТЬ ВСЁ'),
    # multiplayer folder
    ('PG_PLAYER_TITLE', 'PLAYER SETUP', 'НАСТРОЙКА ИГРОКА'),
    ('PG_PLAYER_TICK', '*** YOUR NAME, TEAM AND CHARACTER IN MULTIPLAYER ***', '*** ВАШЕ ИМЯ, КОМАНДА И ПЕРСОНАЖ В СЕТЕВОЙ ИГРЕ ***'),
    ('PG_PLAYER_NAME', 'NAME', 'ИМЯ'),
    ('PG_PLAYER_LOOK', 'CHARACTER', 'ПЕРСОНАЖ'),
    ('PG_PLAYER_TEAM', 'Team', 'Команда'),
    ('PG_PLAYER_MODEL', 'Character', 'Персонаж'),
    ('PG_JOIN_TICK', '*** PICK A SERVER AND CONNECT *** REFRESH QUERIES THE SERVER LIST AGAIN ***',
     '*** ВЫБЕРИТЕ СЕРВЕР И ПОДКЛЮЧИТЕСЬ *** «ОБНОВИТЬ» ЗАНОВО ОПРАШИВАЕТ СПИСОК ***'),
    ('PG_JOIN_NONE', 'No server selected', 'Сервер не выбран'),
    ('PG_JOIN_HOST', 'Server', 'Сервер'),
    ('PG_JOIN_PING', 'Ping', 'Пинг'),
    ('PG_JOIN_PURE', 'Pure', 'Чист.'),
    ('PG_JOIN_GT', 'Mode', 'Режим'),
    ('PG_JOIN_MAP', 'Map', 'Карта'),
    ('PG_JOIN_PLAYERS', 'Players', 'Игроки'),
    ('PG_JOIN_CONNECT', 'CONNECT', 'ПОДКЛЮЧИТЬСЯ'),
    ('PG_JOIN_REFRESH', 'REFRESH', 'ОБНОВИТЬ'),
    ('PG_JOIN_ADV', 'BY ADDRESS', 'ПО АДРЕСУ'),
    ('PG_JOIN_ST_PLAYERS', 'Players', 'Игроков'),
    ('PG_JOIN_ST_SERVERS', 'Servers', 'Серверов'),
    ('PG_JOIN_ST_LOCKED', 'Locked', 'Закрытых'),
    ('PG_JOIN_RULES', 'Server Rules', 'Правила сервера'),
    ('PG_JOIN_NOPLAYERS', 'No players', 'Нет игроков'),
    ('PG_JOIN_MAXCLIENTS', 'Max Players', 'Макс. игроков'),
    ('PG_JOIN_FRAGLIMIT', 'Frag Limit', 'Лимит фрагов'),
    ('PG_JOIN_TIMELIMIT', 'Time Limit', 'Лимит времени'),
    ('PG_JOIN_SPECT', 'Spectators', 'Зрители'),
    ('PG_JOIN_CHEATS', 'Cheats', 'Читы'),
    ('PG_JOIN_PENALTY', 'Suicide Penalty', 'Штраф за самоубийство'),
    ('PG_JOIN_RTEAM', 'Red Team', 'Красная команда'),
    ('PG_JOIN_BTEAM', 'Blue Team', 'Синяя команда'),
    ('PG_JOIN_FCAPT', 'Flag Captures', 'Захватов флага'),
    ('PG_JOIN_MOVESCALE', 'Movement Speed', 'Скорость движения'),
    ('PG_JOIN_VIOL', 'Violence', 'Насилие'),
    ('PG_JOIN_VIOLS', 'Death Animations,Blood,Wounds,Dismemberment,Wound Textures',
     'Анимации смерти,Кровь,Раны,Расчленение,Текстуры ран'),
    ('PG_JOIN_DMFLAGS', 'Game Flags', 'Флаги игры'),
    ('PG_START_TITLE', 'START SERVER', 'СОЗДАНИЕ СЕРВЕРА'),
    ('PG_START_TICK', '*** NAME THE SERVER, PICK THE MAPS AND THE MODE, THEN START ***',
     '*** НАЗОВИТЕ СЕРВЕР, ВЫБЕРИТЕ КАРТЫ И РЕЖИМ, ЗАТЕМ ЗАПУСТИТЕ ***'),
    ('PG_START_HOST', 'SERVER NAME', 'ИМЯ СЕРВЕРА'),
    ('PG_START_PUBLIC', 'Public', 'Публичный'),
    ('PG_START_LIST', 'MAP LIST', 'СПИСОК КАРТ'),
    ('PG_START_MAP', 'Map', 'Карта'),
    ('PG_START_TYPE', 'Mode', 'Режим'),
    ('PG_START_TYPES', 'Deathmatch,Assassin,Arsenal,Capture the Flag,Realistic',
     'Бой насмерть,Убийца,Арсенал,Захват флага,Реализм'),
    ('PG_START_RULES', 'GAME RULES', 'ПРАВИЛА ИГРЫ'),
    ('PG_START_BOTS', 'BOTS', 'БОТЫ'),
    ('PG_START_GO', 'START', 'ЗАПУСТИТЬ'),
    ('PG_START_MAPS', 'ALL MAPS', 'ВСЕ КАРТЫ'),
    ('PG_START_INLIST', 'IN THE LIST', 'В СПИСКЕ'),
    ('PG_OPTIONS_TITLE', 'SERVER OPTIONS', 'ПАРАМЕТРЫ СЕРВЕРА'),
    ('PG_OPTIONS_TICK', '*** NETWORK AND DOWNLOAD SETTINGS FOR MULTIPLAYER ***', '*** НАСТРОЙКИ СЕТИ И ЗАГРУЗОК ДЛЯ СЕТЕВОЙ ИГРЫ ***'),
]
# dmflags bit names (own wording), per game type the bits the type uses
DMF_EN = ['No Health', 'No Items', 'Weapons Stay', 'No Falling Damage', 'Realistic Damage', 'Same Map',
          'No Reloading', 'Team Play', 'No Friendly Fire', 'Forced Respawn', 'No Armour', 'Infinite Ammo',
          'Spinning Pickups', 'Bullets', 'Forced Join']
DMF_RU = ['Без аптечек', 'Без предметов', 'Оружие остаётся', 'Без урона от падения', 'Реалистичный урон',
          'Та же карта', 'Без перезарядки', 'Командная игра', 'Без огня по своим', 'Принудительное возрождение',
          'Без брони', 'Бесконечные патроны', 'Вращение предметов', 'Пули', 'Принудительный вход']
DMF_USE = {1: '111111111111110', 2: '111111100111100', 3: '110111000110000', 4: '111111101111101', 5: '100001000100100'}
for t, mask in DMF_USE.items():
    PG.append(('PG_JOIN_DMF%d' % t, ','.join(DMF_EN[i] if mask[i] == '1' else '' for i in range(15)),
               ','.join(DMF_RU[i] if mask[i] == '1' else '' for i in range(15))))
# key actions
KEYS = [
    ('FORWARD', 'Move Forward', 'Вперёд'), ('BACK', 'Move Back', 'Назад'),
    ('STEP_LEFT', 'Strafe Left', 'Шаг влево'), ('STEP_RIGHT', 'Strafe Right', 'Шаг вправо'),
    ('UP', 'Jump', 'Прыжок'), ('DOWN', 'Crouch', 'Присесть'), ('RUN', 'Run / Walk', 'Бег / шаг'),
    ('USE', 'Use', 'Использовать'), ('LL', 'Lean Left', 'Наклон влево'), ('LR', 'Lean Right', 'Наклон вправо'),
    ('ATTACK', 'Fire', 'Огонь'), ('ALT_ATTACK', 'Alternate Fire', 'Второй режим огня'),
    ('RELOAD', 'Reload', 'Перезарядка'), ('NEXT', 'Next Weapon', 'Следующее оружие'),
    ('PREV', 'Previous Weapon', 'Предыдущее оружие'), ('SAFE', 'Best Weapon (no explosives)', 'Лучшее оружие (без взрывчатки)'),
    ('UNSAFE', 'Best Weapon (any)', 'Лучшее оружие (любое)'), ('DROP_WEAPON', 'Drop Weapon', 'Выбросить оружие'),
    ('WSPECIAL1', 'Weapon Action 1', 'Действие оружия 1'), ('WSPECIAL2', 'Weapon Action 2', 'Действие оружия 2'),
    ('KNIFE', 'Knife', 'Нож'), ('PISTOL1', '9mm Pistol', 'Пистолет 9 мм'), ('PISTOL2', '.44 Pistol', 'Пистолет .44'),
    ('SHOTGUN', 'Shotgun', 'Дробовик'), ('SNIPER', 'Sniper Rifle', 'Снайперская винтовка'),
    ('MPISTOL', 'Silenced SMG', 'Пистолет-пулемёт с глушителем'), ('ASSAULT', 'Submachine Gun', 'Пистолет-пулемёт'),
    ('MACHINEGUN', 'Heavy Machine Gun', 'Тяжёлый пулемёт'), ('SLUGGER', 'Slug Thrower', 'Слаг-метатель'),
    ('ROCKET', 'Rocket Launcher', 'Ракетомёт'), ('FLAMEGUN', 'Flamethrower', 'Огнемёт'), ('MPG', 'Microwave Pulse Gun', 'Микроволновое ружьё'),
    ('USE_ITEM', 'Use Item', 'Применить предмет'), ('NEXT_ITEM', 'Next Item', 'Следующий предмет'),
    ('PREV_ITEM', 'Previous Item', 'Предыдущий предмет'), ('DROP_ITEM', 'Drop Item', 'Выбросить предмет'),
    ('USE_GRENADE', 'Grenade', 'Граната'), ('USE_C4', 'C4 Charge', 'Заряд C4'), ('USE_FPAK', 'Flash Pack', 'Светошумовая граната'),
    ('USE_MEDKIT', 'Medkit', 'Аптечка'), ('USE_GOGGLES', 'Night Goggles', 'Очки ночного видения'),
    ('SS', 'Scores', 'Счёт'), ('CHAT', 'Chat', 'Чат'), ('TCHAT', 'Team Chat', 'Командный чат'),
    ('OBJS', 'Objectives', 'Задачи'), ('HELP', 'Help', 'Справка'),
    ('WAVE1', 'Gesture: Go', 'Жест: вперёд'), ('WAVE2', 'Gesture: Halt', 'Жест: стоп'), ('WAVE3', 'Gesture: Taunt', 'Жест: насмешка'),
    ('LEFT', 'Turn Left', 'Поворот влево'), ('RIGHT', 'Turn Right', 'Поворот вправо'),
    ('LOOK_UP', 'Look Up', 'Взгляд вверх'), ('LOOK_DOWN', 'Look Down', 'Взгляд вниз'),
    ('CENTER', 'Centre View', 'Взгляд прямо'), ('MOUSE_LOOK', 'Mouse Look', 'Обзор мышью'),
    ('KEYBOARD', 'Keyboard Look', 'Обзор клавишами'), ('STRAFE', 'Strafe Modifier', 'Боковой шаг (модификатор)'),
]
PG += [('PG_K_' + k, en, ru) for k, en, ru in KEYS]
PG += [('PG_K_NONE', 'not bound', 'не назначено')]
PG += [('PG_STATS_TITLE', 'STATISTICS', 'СТАТИСТИКА'),
       ('PG_STATS_TICK', '*** STATISTICS OF THE CURRENT GAME ***', '*** СТАТИСТИКА ТЕКУЩЕЙ ИГРЫ ***'),
       ('PG_STATS_NONE', 'No statistics yet. They appear here during a game.', 'Статистики пока нет. Она появляется здесь во время игры.')]
PG += [('PG_LOAD_EMPTY', 'No saved games yet. Saves appear here after F6 / quick save or the Save page.', 'Сохранённых игр пока нет. Они появятся здесь после быстрого сохранения (F6) или со страницы «Сохранить».')]

if __name__ == '__main__':
    video()
    write('menu_omset', 197, 'MENU_OMSET', 'OpenMerc audio, controls, gameplay and multiplayer pages', er(SET))
    # the own folder pages (MENU_OMSET_PG_*): a package of their own, menu_omset is full
    write('menu_ompg', 201, 'MENU_OMSET_PG', 'OpenMerc folder pages, dialogs and key actions',
          er([(r[3:], en, ru) for r, en, ru in PG]))
    write('menu_omovr', 196, 'OMOVR', 'OpenMerc wording for stock references (tab tips, titles, tickers)', er(OVR))
    write('menu_omfill', 198, 'OMFILL', 'OpenMerc texts for stock references without a text', er(FILL))
