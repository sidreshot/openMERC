/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 *
 * =======================================================================
 *
 * This file implements the console
 *
 * =======================================================================
 */

#include "header/client.h"
#include "../common/header/om_version.h"
#include "header/sofclient.h" /* OpenMerc */
#include <time.h>

console_t con;
cvar_t *con_notifytime;

extern char key_lines[NUM_KEY_LINES][MAXCMDLINE];
extern int edit_line;
extern int key_linepos;

/* MENU-SCRATCH (fonts brief): a console character in the monospaced OFL
   face (con_ttf 1), else the stock bitmap character */
static void
Con_CharScaled(int x, int y, int ch, float scale)
{
	if (!MR_ConsoleChar(x, y, ch, (int)(8 * scale + 0.5f)))
	{
		Draw_CharScaled(x, y, ch, scale);
	}
}

/* pixels from one text row to the next: the 8 unit grid for the bitmap
   font, the face's line height (ink, shadow, gap) for the OFL face */
static int
Con_Pitch(float scale)
{
	return MR_ConsolePitch((int)(8 * scale + 0.5f));
}

static void
Con_StringScaled(int x, int y, const char *s, float scale)
{
	for (; *s; s++, x += 8 * scale)
	{
		Con_CharScaled(x, y, *s, scale);
	}
}

void
DrawStringScaled(int x, int y, const char *s, float factor)
{
	while (*s)
	{
		Draw_CharScaled(x, y, *s, factor);
		x += 8*factor;
		s++;
	}
}

void
DrawAltStringScaled(int x, int y, const char *s, float factor)
{
	while (*s)
	{
		/* OpenMerc: SoF fonts have no Quake II alternate (high-bit) charset;
		   glyphs 128..255 are Latin-1 / CP1251 letters (SPEC-132) */
		Draw_CharScaled(x, y, *s, factor);
		x += 8*factor;
		s++;
	}
}

void
Key_ClearTyping(void)
{
	key_lines[edit_line][0] = '\0';
	key_linepos = 0;
}

void
Con_ToggleConsole_f(void)
{
	/* OpenMerc: SPEC-312 5.1 - only with the console cvar set; a cinematic
	   freeze turns it into a skip request (cl_keyboard.c) */
	if (!Key_ConsoleToggleAllowed())
	{
		return;
	}

	SCR_EndLoadingPlaque(); /* get rid of loading plaque */

	if (cl.attractloop || CL_Demo_Playing())
	{
		/* OpenMerc: demos play in the client (cl_demo.c); ending one is a disconnect */
		Cbuf_AddText(CL_Demo_Playing() ? "disconnect\n" : "killserver\n");
		return;
	}

	if (cls.state == ca_disconnected)
	{
		/* OpenMerc: 1.07f started the attract demo here (SPEC-312 5.1);
		   the console opens over the start-up menu instead, and closing
		   it brings the main menu back */
		Key_ClearTyping();

		if (cls.key_dest == key_console)
		{
			M_ForceMenuOff();
			cls.key_dest = key_game;
			Cbuf_AddText("menu main\n");
		}
		else
		{
			M_ForceMenuOff();
			cls.key_dest = key_console;
		}

		return;
	}

	Key_ClearTyping();
	Con_ClearNotify();

	if (cls.key_dest == key_console)
	{
		/* the SoF menu close (M_CloseSystem) only hands the keyboard back
		   from key_menu, so the console gives it back to the game itself
		   (before, the console stayed open until Escape) */
		M_ForceMenuOff();
		cls.key_dest = key_game;
		Cvar_Set("paused", "0");
	}
	else
	{
		M_ForceMenuOff();
		cls.key_dest = key_console;

		if ((Cvar_VariableValue("maxclients") == 1) &&
			Com_ServerState())
		{
			Cvar_Set("paused", "1");
		}
	}
}

void
Con_ToggleChat_f(void)
{
	Key_ClearTyping();

	if (cls.key_dest == key_console)
	{
		if (cls.state == ca_active)
		{
			M_ForceMenuOff();
			cls.key_dest = key_game;
		}
	}
	else
	{
		cls.key_dest = key_console;
	}

	Con_ClearNotify();
}

void
Con_Clear_f(void)
{
	memset(con.text, ' ', CON_TEXTSIZE);
}

/*
 * Save the console contents out to a file
 */
void
Con_Dump_f(void)
{
	int l, x;
	char *line;
	FILE *f;
	char buffer[1024];
	char name[MAX_OSPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf("usage: condump <filename>\n");
		return;
	}

	if (con.linewidth >= 1024)
	{
		Com_Printf("con.linewidth too large!\n");
		return;
	}

	Com_sprintf(name, sizeof(name), "%s/%s.txt", FS_Gamedir(), Cmd_Argv(1));

	Com_Printf("Dumped console text to %s.\n", name);
	FS_CreatePath(name);
	f = Q_fopen(name, "w");

	if (!f)
	{
		Com_Printf("ERROR: couldn't open.\n");
		return;
	}

	/* skip empty lines */
	for (l = con.current - con.totallines + 1; l <= con.current; l++)
	{
		line = con.text + (l % con.totallines) * con.linewidth;

		for (x = 0; x < con.linewidth; x++)
		{
			if (line[x] != ' ')
			{
				break;
			}
		}

		if (x != con.linewidth)
		{
			break;
		}
	}

	/* write the remaining lines */
	buffer[con.linewidth] = 0;

	for ( ; l <= con.current; l++)
	{
		line = con.text + (l % con.totallines) * con.linewidth;
		memcpy(buffer, line, con.linewidth);

		for (x = con.linewidth - 1; x >= 0; x--)
		{
			if (buffer[x] == ' ')
			{
				buffer[x] = 0;
			}

			else
			{
				break;
			}
		}

		for (x = 0; buffer[x]; x++)
		{
			buffer[x] &= 0x7f;
		}

		fprintf(f, "%s\n", buffer);
	}

	fclose(f);
}

void
Con_ClearNotify(void)
{
	int i;

	for (i = 0; i < NUM_CON_TIMES; i++)
	{
		con.times[i] = 0;
	}
}

void
Con_MessageMode_f(void)
{
	chat_team = false;
	cls.key_dest = key_message;
}

void
Con_MessageMode2_f(void)
{
	chat_team = true;
	cls.key_dest = key_message;
}

/*
 * If the line width has changed, reformat the buffer.
 */
void
Con_CheckResize(void)
{
	int i, j, width, oldwidth, oldtotallines, numlines, numchars;
	char tbuf[CON_TEXTSIZE];
	float scale = SCR_GetConsoleScale();

	/* We need to clamp the line width to MAXCMDLINE - 2,
	   otherwise we may overflow the text buffer if the
	   vertical resultion / 8 (one char == 8 pixels) is
	   bigger then MAXCMDLINE.
	   MAXCMDLINE - 2 because 1 for the prompt and 1 for
	   the terminating \0. */
	width = ((int)(viddef.width / scale) / 8) - 2;
	width = width > MAXCMDLINE - 2 ? MAXCMDLINE - 2 : width;

	if (width == con.linewidth)
	{
		return;
	}

	/* video hasn't been initialized yet */
	if (width < 1)
	{
		width = 38;
		con.linewidth = width;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		memset(con.text, ' ', CON_TEXTSIZE);
	}
	else
	{
		oldwidth = con.linewidth;
		con.linewidth = width;
		oldtotallines = con.totallines;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		numlines = oldtotallines;

		if (con.totallines < numlines)
		{
			numlines = con.totallines;
		}

		numchars = oldwidth;

		if (con.linewidth < numchars)
		{
			numchars = con.linewidth;
		}

		memcpy(tbuf, con.text, CON_TEXTSIZE);
		memset(con.text, ' ', CON_TEXTSIZE);

		for (i = 0; i < numlines; i++)
		{
			for (j = 0; j < numchars; j++)
			{
				con.text[(con.totallines - 1 - i) * con.linewidth + j] =
					tbuf[((con.current - i + oldtotallines) %
						  oldtotallines) * oldwidth + j];
			}
		}

		Con_ClearNotify();
	}

	con.current = con.totallines - 1;
	con.display = con.current;
}

void
Con_Init(void)
{
	con.linewidth = -1;

	Con_CheckResize();

	Com_Printf("Console initialized.\n");

	/* register our commands */
	con_notifytime = Cvar_Get("con_notifytime", "3", 0);

	Cmd_AddCommand("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand("togglechat", Con_ToggleChat_f);
	Cmd_AddCommand("messagemode", Con_MessageMode_f);
	Cmd_AddCommand("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand("clear", Con_Clear_f);
	Cmd_AddCommand("condump", Con_Dump_f);
	con.initialized = true;
}

void
Con_Linefeed(void)
{
	con.x = 0;

	if (con.display == con.current)
	{
		con.display++;
	}

	con.current++;
	memset(&con.text[(con.current % con.totallines) * con.linewidth],
			' ', con.linewidth);
}

/*
 * Handles cursor positioning, line wrapping, etc All console printing
 * must go through this in order to be logged to disk If no console is
 * visible, the text will appear at the top of the game window
 */
void
Con_Print(char *txt)
{
	int y;
	int c, l;
	static int cr;
	int mask;
	qboolean quiet;

	if (!con.initialized)
	{
		return;
	}

	if ((txt[0] == 1) || (txt[0] == 2))
	{
		mask = 0; /* OpenMerc: Quake II coloured (high-bit) text would pick CP1251 letters of the SoF font */
		txt++;
	}
	else
	{
		mask = 0;
	}

	/* OpenMerc: OMsnd debug lines (s_om_debug, "[p=<time>]") go to the console and the logs, not to the overlay
	   (the test build records with s_om_debug 24: one line per second would cover the top of the screen) */
	quiet = !strncmp(txt, "OMsnd: [p=", 10);

	while ((c = *txt))
	{
		/* count word length */
		for (l = 0; l < con.linewidth; l++)
		{
			if ((unsigned char)txt[l] <= ' ')
			{
				break;
			}
		}

		/* word wrap */
		if ((l != con.linewidth) && (con.x + l > con.linewidth))
		{
			con.x = 0;
		}

		txt++;

		if (cr)
		{
			con.current--;
			cr = false;
		}

		if (!con.x)
		{
			Con_Linefeed();

			/* mark time for transparent overlay */
			if (con.current >= 0)
			{
				con.times[con.current % NUM_CON_TIMES] = quiet ? 0 : cls.realtime;
			}
		}

		switch (c)
		{
			case '\n':
				con.x = 0;
				break;

			case '\r':
				con.x = 0;
				cr = 1;
				break;

			default: /* display character and advance */
				y = con.current % con.totallines;
				con.text[y * con.linewidth + con.x] = c | mask | con.ormask;
				con.x++;

				if (con.x >= con.linewidth)
				{
					con.x = 0;
				}

				break;
		}
	}
}

/*
 * The input line scrolls horizontally if
 * typing goes beyond the right edge
 */
void
Con_DrawInput(void)
{
	int i;
	float scale;
	char *text;
	char ch;
	size_t txtlen;
	int linepos;
	int draw_icon;

	if (cls.key_dest == key_menu)
	{
		return;
	}

	/* don't draw anything (always draw if not active) */
	if ((cls.key_dest != key_console) && (cls.state == ca_active))
	{
		return;
	}

	scale = SCR_GetConsoleScale();
	text = key_lines[edit_line];
	linepos = key_linepos;

	/* prestep if horizontally scrolling */
	if (linepos >= (con.linewidth - 1))
	{
		int ofs = 1 + linepos - con.linewidth;

		text += ofs;
		linepos -= ofs;

		draw_icon = 0;
	}
	else
	{
		Draw_CharScaled(8 * scale, con.vislines - 22 * scale, CON_INPUT_INDICATOR, scale);
		draw_icon = 1;
	}

	txtlen = strlen(text);

	for (i = 0; i < (con.linewidth - draw_icon); i++)
	{
		if (i == linepos)
		{
			if ((cls.realtime >> 8) & 1)
			{
				ch = CON_INPUT_CURSOR;
			}
			else
			{
				ch = (text[i] == '\0') ? 10 : text[i];
			}
		}
		else if (i >= txtlen)
		{
			ch = ' ';
		}
		else
		{
			ch = text[i];
		}

		Con_CharScaled(((i + 1 + draw_icon) << 3) * scale, con.vislines - 22 * scale, ch, scale);
	}
}

/*
 * Draws the last few lines of output transparently over the game top
 */
void
Con_DrawNotify(void)
{
	int x, v;
	char *text;
	int i;
	int time;
	char *s;
	int skip;
	float scale;
	int pitch;

	v = 0; /* pixels */
	scale = SCR_GetConsoleScale();
	pitch = Con_Pitch(scale);

	for (i = con.current - NUM_CON_TIMES + 1; i <= con.current; i++)
	{
		if (i < 0)
		{
			continue;
		}

		time = con.times[i % NUM_CON_TIMES];

		if (time == 0)
		{
			continue;
		}

		time = cls.realtime - time;

		if (time > con_notifytime->value * 1000)
		{
			continue;
		}

		text = con.text + (i % con.totallines) * con.linewidth;

		for (x = 0; x < con.linewidth; x++)
		{
			Con_CharScaled(((x + 1) << 3) * scale, v, text[x], scale);
		}

		v += pitch;
	}

	if (cls.key_dest == key_message)
	{
		if (chat_team)
		{
			Con_StringScaled(8 * scale, v, "say_team:", scale);
			skip = 11;
		}
		else
		{
			Con_StringScaled(8 * scale, v, "say:", scale);
			skip = 5;
		}

		s = chat_buffer;

		if (chat_bufferlen > (viddef.width >> 3) - (skip + 1))
		{
			s += chat_bufferlen - ((viddef.width >> 3) - (skip + 1));
		}

		x = 0;

		while (s[x])
		{
			Con_CharScaled(((x + skip) << 3) * scale, v, s[x], scale);
			x++;
		}

		Con_CharScaled(((x + skip) << 3) * scale, v, 10 + ((cls.realtime >> 8) & 1), scale);
		v += pitch;
	}

	if (v)
	{
		SCR_AddDirtyPoint(0, 0);
		SCR_AddDirtyPoint(viddef.width - 1, v);
	}
}

/*
 * Draws the console with the solid background
 */
void
Con_DrawConsole(float frac)
{
	int i, j, x, y, n;
	int rows;
	size_t verLen;
	char *text;
	int row;
	int lines;
	int stampx;
	int pitch;
	float scale;
	char version[48];
	char dlbar[1024];
	char timebuf[48];
	char tmpbuf[48];

	time_t t;
	struct tm *today;

	scale = SCR_GetConsoleScale();
	lines = viddef.height * frac;

	if (lines <= 0)
	{
		return;
	}

	if (lines > viddef.height)
	{
		lines = viddef.height;
	}

	/* draw the background (SoF: pics/console/conback.m32, found first by the
	   renderer's picture lookup); looked up once, so a data set without it
	   gets a plain fill instead of a failing lookup every frame */
	{
		static int conback = 0; /* 0 not probed, 1 present, -1 missing */

		if (!conback)
		{
			conback = Draw_FindPic("conback") ? 1 : -1;
		}

		if (conback > 0)
		{
			Draw_StretchPic(0, -viddef.height + lines, viddef.width,
					viddef.height, "conback");
		}
		else
		{
			Draw_Fill(0, 0, viddef.width, lines, 0);
		}
	}
	SCR_AddDirtyPoint(0, 0);
	SCR_AddDirtyPoint(viddef.width - 1, lines - 1);

	/* OpenMerc: the openMERC version, not the base engine's; plain characters:
	   the upper half of the SoF console font (the Quake 2 coloured set) holds
	   the Cyrillic letters of the Russian release */
	Q_strlcpy(version, OM_VERSION_STRING, sizeof(version));

	verLen = strlen(version);

	for (x = 0; x < verLen; x++)
	{
		Con_CharScaled(viddef.width - ((verLen*8+5) * scale) + x * 8 * scale, lines - 35 * scale, version[x], scale);
	}

	t = time(NULL);
	today = localtime(&t);
	strftime(timebuf, sizeof(timebuf), "%H:%M:%S - %m/%d/%Y", today);

	Com_sprintf(tmpbuf, sizeof(tmpbuf), "%s", timebuf);

	for (x = 0; x < 21; x++)
	{
		Con_CharScaled(viddef.width - (173 * scale) + x * 8 * scale, lines - 25 * scale, tmpbuf[x], scale);
	}

	/* MENU-SCRATCH: text rows that reach the version and clock stamp end
	   before it (long lines ran through the stamp); stampx in character
	   columns of the text, which starts at column 1 */
	stampx = (int)((viddef.width - ((int)verLen * 8 + 5 > 173 ? (int)verLen * 8 + 5 : 173) * scale) / (8 * scale)) - 2;

	/* draw the text */
	con.vislines = lines;

	/* rows of text to draw, from the bottom up; y in pixels, the top of the
	   bottom row one pitch above the input line */
	pitch = Con_Pitch(scale);
	rows = (int)((lines - 22 * scale) / pitch) + 1;
	y = (int)(lines - 22 * scale) - pitch;

	/* draw from the bottom up */
	if (con.display != con.current)
	{
		/* draw arrows to show the buffer is backscrolled */
		for (x = 0; x < con.linewidth; x += 4)
		{
			Draw_CharScaled(((x + 1) << 3) * scale, y, '^', scale);
		}

		y -= pitch;
		rows--;
	}

	row = con.display;

	for (i = 0; i < rows; i++, y -= pitch, row--)
	{
		if (row < 0)
		{
			break;
		}

		if (con.current - row >= con.totallines)
		{
			break; /* past scrollback wrap point */
		}

		text = con.text + (row % con.totallines) * con.linewidth;

		for (x = 0; x < con.linewidth; x++)
		{
			if (x >= stampx && y + pitch > lines - 35 * scale)
			{
				break;
			}

			Con_CharScaled(((x + 1) << 3) * scale, y, text[x], scale);
		}
	}

	/* draw the download bar, figure out width */
#ifdef USE_CURL
	if (cls.downloadname[0] && (cls.download || cls.downloadposition))
#else
	if (cls.download)
#endif
	{
		if ((text = strrchr(cls.downloadname, '/')) != NULL)
		{
			text++;
		}

		else
		{
			text = cls.downloadname;
		}

		x = con.linewidth - ((con.linewidth * 7) / 40);
		y = x - strlen(text) - 8;
		i = con.linewidth / 3;

		if (strlen(text) > i)
		{
			y = x - i - 11;
			memcpy(dlbar, text, i);
			dlbar[i] = 0;
			strcat(dlbar, "...");
		}
		else
		{
			strcpy(dlbar, text);
		}

		strcat(dlbar, ": ");
		i = strlen(dlbar);
		dlbar[i++] = '\x80';

		/* where's the dot gone? */
		if (cls.downloadpercent == 0)
		{
			n = 0;
		}

		else
		{
			n = y * cls.downloadpercent / 100;
		}

		for (j = 0; j < y; j++)
		{
			if (j == n)
			{
				dlbar[i++] = '\x83';
			}

			else
			{
				dlbar[i++] = '\x81';
			}
		}

		dlbar[i++] = '\x82';
		dlbar[i] = 0;

		sprintf(dlbar + strlen(dlbar), " %02d%%", cls.downloadpercent);

		/* draw it */
		y = (lines - 12 * scale) / scale;

		for (i = 0; i < strlen(dlbar); i++)
		{
			Draw_CharScaled(((i + 1) << 3) * scale, y * scale, dlbar[i], scale);
		}
	}

	/* draw the input prompt, user text, and cursor if desired */
	Con_DrawInput();
}
