# Menu fonts (openMERC's own, not game data)

TrueType fonts of the menu text (engine/src/client/menu/m_font.c), from the
Google Fonts repository (github.com/google/fonts, ofl/), unmodified:

| File | Font | License |
| --- | --- | --- |
| Play-Regular.ttf, Play-Bold.ttf | Play, Jonas Hecksher (Playtypes, e-types AS) | SIL Open Font License 1.1, OFL-Play.txt |
| RussoOne-Regular.ttf | Russo One, Jovanny Lemonad | SIL Open Font License 1.1, OFL-RussoOne.txt |
| JetBrainsMono-Regular.ttf | JetBrains Mono 2.x, The JetBrains Mono Project Authors (github.com/JetBrains/JetBrainsMono, fonts/ttf/, unmodified) | SIL Open Font License 1.1, OFL-JetBrainsMono.txt |

All cover Latin and Cyrillic (every printable CP1251 character, checked with fontTools) (the menus are CP1251). The OFL allows
redistribution bundled with software; the fonts are not sold by themselves.

JetBrains Mono draws the console text (MF_DrawCell, cvar con_ttf; 0 = the
stock 8x8 console font).
