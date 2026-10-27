/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Soldier of Fortune 1.07f string packages at run time: strip/<name>.spe or
 * strip/<name>.sp is parsed line by line, the text of the selected
 * language (sp_language) is kept per entry with the colour tokens replaced
 * by their bytes, packages are filed by ID with owner bits, and texts are
 * looked up by string id or by PACKAGE_ENTRY name. Changing sp_language
 * reloads every package.
 *
 * Implements: SPEC-131, SPEC-313 (§1, "String-package keywords", "Colour
 *             tokens", "Package owners", "Answers to open questions")
 */

#include "header/common.h"

#define SP_LINE_LIMIT 8192

typedef struct
{
	char *text;  /* NULL = empty slot */
	char *name;  /* entry REFERENCE or NULL */
	int flags;
} spentry_t;

typedef struct sppackage_s
{
	qboolean used;
	int id;
	int owners;
	char regname[MAX_QPATH];   /* name given to SP_Register */
	char reference[MAX_QPATH]; /* header REFERENCE */
	spentry_t entries[SP_MAX_ENTRIES];
} sppackage_t;

static sppackage_t sp_packages[SP_MAX_PACKAGES]; /* index = package ID */
static cvar_t *sp_language;
static cvar_t *om_language; /* MENU2: auto / en / ru (TEXT_RUSSIAN) */
static cvar_t *sv_show_strip;
static qboolean sp_reloading;

static const struct
{
	const char *token;
	byte code;
} sp_colours[] = {
	{"$P_WHITE", 0x01}, {"$P_RED", 0x02}, {"$P_GREEN", 0x03},
	{"$P_YELLOW", 0x04}, {"$P_BLUE", 0x05}, {"$P_PURPLE", 0x06},
	{"$P_CYAN", 0x07}, {"$P_BLACK", 0x08}, {"$P_HWHITE", 0x09},
	{"$P_HRED", 0x0B}, {"$P_HGREEN", 0x0C}, {"$P_HYELLOW", 0x0E},
	{"$P_HBLUE", 0x0F}, {"$P_CAMOBROWN", 0x10}, {"$P_CAMOGREEN", 0x11},
	{"$P_SEAGREEN", 0x12}, {"$P_SEABLUE", 0x13}, {"$P_METAL", 0x14},
	{"$P_DBLUE", 0x15}, {"$P_DPURPLE", 0x16}, {"$P_DGREY", 0x17},
	{"$P_PINK", 0x18}, {"$P_BLOODRED", 0x19}, {"$P_RUSSET", 0x1A},
	{"$P_BROWN", 0x1B}, {"$P_TEXT", 0x1C}, {"$P_BAIGE", 0x1D},
	{"$P_LBROWN", 0x1E}, {"$P_ORANGE", 0x1F}
};

static const struct
{
	const char *name;
	int value;
} sp_flagnames[] = {
	{"SP_FLAG_CENTERED", SP_FLAG_CENTERED},
	{"SP_FLAG_TYPEAMATIC", SP_FLAG_TYPEAMATIC},
	{"SP_FLAG_CAPTIONED", SP_FLAG_CAPTIONED},
	{"SP_FLAG_CREDIT", SP_FLAG_CREDIT},
	{"SP_FLAG_LAYOUT", SP_FLAG_LAYOUT},
	{"SP_FLAG_ORIGINAL", SP_FLAG_ORIGINAL},
	{"SP_FLAG_ALWAYS_PRINT", SP_FLAG_ALWAYS_PRINT},
	{"SP_FLAG_BLINK", SP_FLAG_BLINK}
};

typedef enum
{
	KW_UNKNOWN, KW_VERSION, KW_ID, KW_REFERENCE, KW_DESCRIPTION, KW_COUNT,
	KW_FLAGS, KW_INDEX, KW_OPEN, KW_CLOSE, KW_NOTES, KW_TEXT, KW_FLAGNAME
} spkeyword_t;

/* ---------------------------------------------------------------------
 * parsing
 * --------------------------------------------------------------------- */

typedef struct
{
	const char *p;     /* next line */
	const char *end;
	char keyword[64];
	const char *value; /* start of the value in the text */
	const char *eol;   /* end of the line (LF or end of text) */
} spreader_t;

/* reads the next line; false at the end of the text */
static qboolean
SP_NextLine(spreader_t *r)
{
	const char *s, *lf;
	size_t k;

	if (r->p >= r->end)
	{
		return false;
	}

	s = r->p;
	lf = memchr(s, '\n', r->end - s);
	r->eol = lf ? lf : r->end;

	if (r->eol - s > SP_LINE_LIMIT)
	{
		r->eol = s + SP_LINE_LIMIT;
	}

	r->p = lf ? lf + 1 : r->end;

	/* indentation of entry lines */
	while ((s < r->eol) && ((*s == ' ') || (*s == '\t')))
	{
		s++;
	}

	/* the keyword runs up to a space, CR or LF */
	k = 0;

	while ((s < r->eol) && (*s != ' ') && (*s != '\r') && (*s != '\t'))
	{
		if (k < sizeof(r->keyword) - 1)
		{
			r->keyword[k++] = *s;
		}

		s++;
	}

	r->keyword[k] = 0;

	while ((s < r->eol) && ((*s == ' ') || (*s == '\t')))
	{
		s++;
	}

	r->value = s;
	return true;
}

static spkeyword_t
SP_Keyword(const char *k, int *language)
{
	static const struct
	{
		const char *name;
		spkeyword_t kw;
	} words[] = {
		{"VERSION", KW_VERSION}, {"ID", KW_ID}, {"REFERENCE", KW_REFERENCE},
		{"DESCRIPTION", KW_DESCRIPTION}, {"COUNT", KW_COUNT},
		{"FLAGS", KW_FLAGS}, {"INDEX", KW_INDEX}, {"{", KW_OPEN},
		{"}", KW_CLOSE}, {"NOTES", KW_NOTES}
	};
	/* index 4 TEXT_RUSSIAN: OpenMerc extension (MENU2), only in OpenMerc's
	   own packages */
	static const char *texts[5] = {
		"TEXT_ENGLISH", "TEXT_GERMAN", "TEXT_FRENCH", "TEXT_BRITISH", "TEXT_RUSSIAN"
	};
	int i;

	for (i = 0; i < (int)(sizeof(words) / sizeof(words[0])); i++)
	{
		if (!Q_stricmp(k, words[i].name))
		{
			return words[i].kw;
		}
	}

	for (i = 0; i < 5; i++)
	{
		if (!Q_stricmp(k, texts[i]))
		{
			*language = i;
			return KW_TEXT;
		}
	}

	for (i = 0; i < (int)(sizeof(sp_flagnames) / sizeof(sp_flagnames[0])); i++)
	{
		if (!Q_stricmp(k, sp_flagnames[i].name))
		{
			return KW_FLAGNAME;
		}
	}

	return KW_UNKNOWN;
}

/*
 * OpenMerc MENU2 (docs/DEVIATIONS.md "Russian strings"): localised installs
 * such as the Russian one keep their language in the TEXT_ENGLISH slot of the
 * stock packages. om_language auto (default) treats the install as Russian
 * when the TEXT_ENGLISH texts of strip/menu_generic.sp contain at least 16
 * CP1251 letters (bytes 0xC0-0xFF); "ru" / "en" force it. While Russian is
 * active a TEXT_RUSSIAN text wins over every other text of its entry, so
 * OpenMerc's own packages follow the language of the stock ones.
 */
static int sp_rudetected = -1;

static qboolean
SP_DetectRussian(void)
{
	char *buf;
	int len, i, n = 0;

	if (sp_rudetected >= 0)
	{
		return sp_rudetected != 0;
	}

	sp_rudetected = 0;
	len = FS_LoadFile("strip/menu_generic.sp", (void **)&buf);

	if (len <= 0)
	{
		return false;
	}

	for (i = 0; i + 12 < len; i++)
	{
		if (!Q_strncasecmp(buf + i, "TEXT_ENGLISH", 12))
		{
			int j = i + 12;

			while ((j < len) && (buf[j] != '\n'))
			{
				if ((unsigned char)buf[j] >= 0xC0)
				{
					n++;
				}

				j++;
			}

			i = j;
		}
	}

	FS_FreeFile(buf);
	sp_rudetected = (n >= 16) ? 1 : 0;
	return sp_rudetected != 0;
}

qboolean
SP_RussianActive(void)
{
	const char *v = om_language ? om_language->string : "auto";

	if (!Q_stricmp(v, "ru") || !Q_stricmp(v, "russian"))
	{
		return true;
	}

	if (!Q_stricmp(v, "en") || !Q_stricmp(v, "english"))
	{
		return false;
	}

	return SP_DetectRussian();
}

/*
 * The value of the current line: a quoted value runs to the first quote
 * directly followed by CR, LF or the end of the text (it may continue on
 * later lines); "\n" becomes a line feed. Unquoted: to the end of the line.
 * Returns a Z_Malloc'ed string.
 */
static char *
SP_ReadValue(spreader_t *r)
{
	const char *s = r->value;
	const char *e;
	char *out, *o;

	if ((s < r->end) && (*s == '"'))
	{
		s++;

		for (e = s; e < r->end; e++)
		{
			if ((*e == '"') &&
				((e + 1 >= r->end) || (e[1] == '\r') || (e[1] == '\n')))
			{
				break;
			}
		}

		/* the value ended on a later line: continue reading after it */
		if (e >= r->eol)
		{
			const char *lf = memchr(e, '\n', r->end - e);

			r->p = lf ? lf + 1 : r->end;
		}
	}
	else
	{
		e = r->eol;

		while ((e > s) && ((e[-1] == '\r') || (e[-1] == ' ')))
		{
			e--;
		}
	}

	out = o = Z_Malloc((int)(e - s) + 1);

	while (s < e)
	{
		if ((s[0] == '\\') && (s + 1 < e) && (s[1] == 'n'))
		{
			*o++ = '\n';
			s += 2;
			continue;
		}

		*o++ = *s++;
	}

	*o = 0;
	return out;
}

/* colour tokens to bytes, sv_show_strip prefix (SPEC-313 §1) */
static char *
SP_StoreText(const char *in)
{
	size_t n = strlen(in);
	qboolean strip = sv_show_strip && sv_show_strip->value;
	char *out, *o;
	const char *s;

	out = o = Z_Malloc((int)n + 8);

	if (strip)
	{
		memcpy(o, "STRIP-", 6);
		o += 6;
	}

	for (s = in; *s; )
	{
		if (*s == '$')
		{
			int i;
			qboolean hit = false;

			for (i = 0; i < (int)(sizeof(sp_colours) / sizeof(sp_colours[0])); i++)
			{
				size_t l = strlen(sp_colours[i].token);

				if (!strncmp(s, sp_colours[i].token, l))
				{
					*o++ = (char)sp_colours[i].code;
					s += l;
					hit = true;
					break;
				}
			}

			if (hit)
			{
				continue;
			}
		}

		*o++ = *s++;
	}

	*o = 0;
	return out;
}

static void
SP_FreeEntries(spentry_t *entries)
{
	int i;

	for (i = 0; i < SP_MAX_ENTRIES; i++)
	{
		Z_Free(entries[i].text);
		Z_Free(entries[i].name);
	}

	memset(entries, 0, sizeof(spentry_t) * SP_MAX_ENTRIES);
}

/*
 * Parses a package into pkg (entries, id, reference). Whatever was read
 * before an error stays; false only when the first line is not VERSION 1.
 */
static qboolean
SP_Parse(const char *text, int len, sppackage_t *pkg)
{
	spreader_t r;
	int language, lang;
	int count, done;
	qboolean ru = SP_RussianActive();

	lang = sp_language ? (int)sp_language->value : 0;

	if ((lang < 0) || (lang > 3))
	{
		lang = 0;
	}

	memset(&r, 0, sizeof(r));
	r.p = text;
	r.end = text + len;

	pkg->id = 0;
	pkg->reference[0] = 0;

	if (!SP_NextLine(&r) || (SP_Keyword(r.keyword, &language) != KW_VERSION) ||
		(atoi(r.value) != 1))
	{
		return false;
	}

	/* header */
	count = -1;

	while (SP_NextLine(&r))
	{
		spkeyword_t kw = SP_Keyword(r.keyword, &language);
		char *v;

		if (kw == KW_UNKNOWN)
		{
			return true;
		}

		if (kw == KW_ID)
		{
			pkg->id = atoi(r.value) & 0xFF;
		}
		else if (kw == KW_REFERENCE)
		{
			v = SP_ReadValue(&r);
			Q_strlcpy(pkg->reference, v, sizeof(pkg->reference));
			Z_Free(v);
		}
		else if (kw == KW_COUNT)
		{
			count = atoi(r.value);
			break;
		}
		else if ((kw == KW_DESCRIPTION) || (kw == KW_NOTES))
		{
			v = SP_ReadValue(&r); /* may span lines */
			Z_Free(v);
		}
	}

	if (count < 0)
	{
		return true;
	}

	/* exactly count entries: INDEX i, {, ..., } */
	for (done = 0; done < count; done++)
	{
		spentry_t *e;
		int slot;

		if (!SP_NextLine(&r) || (SP_Keyword(r.keyword, &language) != KW_INDEX))
		{
			return true;
		}

		slot = atoi(r.value);

		if ((slot < 0) || (slot >= SP_MAX_ENTRIES))
		{
			/* the original does not check; OpenMerc rejects (SPEC-313) */
			return true;
		}

		if (!SP_NextLine(&r) || (SP_Keyword(r.keyword, &language) != KW_OPEN))
		{
			return true;
		}

		e = &pkg->entries[slot];

		{
		qboolean rutaken = false; /* MENU2: a TEXT_RUSSIAN text was taken */

		while (1)
		{
			spkeyword_t kw;

			if (!SP_NextLine(&r))
			{
				return true;
			}

			kw = SP_Keyword(r.keyword, &language);

			if (kw == KW_CLOSE)
			{
				break;
			}

			if (kw == KW_REFERENCE)
			{
				Z_Free(e->name);
				e->name = SP_ReadValue(&r);
			}
			else if (kw == KW_FLAGS)
			{
				/* flag names separated by spaces; an unknown word ends
				   the list */
				char *v = SP_ReadValue(&r);
				char *w = v;

				while (*w)
				{
					char *sp;
					int i, found = 0;

					while (*w == ' ' || *w == '\t')
					{
						w++;
					}

					if (!*w)
					{
						break;
					}

					sp = w;

					while (*sp && (*sp != ' ') && (*sp != '\t'))
					{
						sp++;
					}

					if (*sp)
					{
						*sp++ = 0;
					}

					for (i = 0; i < (int)(sizeof(sp_flagnames) / sizeof(sp_flagnames[0])); i++)
					{
						if (!Q_stricmp(w, sp_flagnames[i].name))
						{
							e->flags |= sp_flagnames[i].value;
							found = 1;
							break;
						}
					}

					if (!found)
					{
						break;
					}

					w = sp;
				}

				Z_Free(v);
			}
			else if (kw == KW_NOTES)
			{
				Z_Free(SP_ReadValue(&r));
			}
			else if (kw == KW_TEXT)
			{
				/* English when nothing is stored yet or L = 0; another
				   language only when it is L */
				qboolean take = (language == 0) ? (!e->text || (lang == 0)) :
					(language == lang);
				char *v = SP_ReadValue(&r);

				/* MENU2: TEXT_RUSSIAN only while Russian is active, and then
				   it wins over the entry's other texts */
				if (language == 4)
				{
					take = ru;
					rutaken = rutaken || ru;
				}
				else if (rutaken)
				{
					take = false;
				}

				if (take)
				{
					Z_Free(e->text);
					e->text = SP_StoreText(v);
				}

				Z_Free(v);
			}
			else
			{
				return true; /* unknown keyword inside an entry */
			}
		}
		}
	}

	return true;
}

/* ---------------------------------------------------------------------
 * registry
 * --------------------------------------------------------------------- */

/*
 * OpenMerc: an entry the winning copy of a package leaves empty is taken from
 * a copy of the same package (same ID) that the winning one shadows. The
 * Russian localisation's zmain.pak carries a singleplr.sp with COUNT 14 over
 * pak2's COUNT 15, which hides 0x060e "Check your fire!" (SPEC-AMEND-014
 * R14.2 item 6: pak2's override must be in effect). Entries the winning copy
 * defines are never replaced.
 */
static void
SP_FillFromShadowed(const char *path, sppackage_t *pkg)
{
	static sppackage_t low; /* large; kept off the stack */
	int n, i;

	for (n = 1; n < 16; n++)
	{
		char *buf;
		int len = FS_LoadFileShadowed(path, n, (void **)&buf);

		if (len < 0)
		{
			return;
		}

		memset(&low, 0, sizeof(low));

		if (SP_Parse(buf, len, &low) && (low.id == pkg->id))
		{
			for (i = 0; i < SP_MAX_ENTRIES; i++)
			{
				if (!pkg->entries[i].text && low.entries[i].text)
				{
					Z_Free(pkg->entries[i].name);
					pkg->entries[i] = low.entries[i];
					memset(&low.entries[i], 0, sizeof(low.entries[i]));
				}
			}
		}

		SP_FreeEntries(low.entries);
		FS_FreeFile(buf);
	}
}

/* loads the package file into a temporary package; false when no file */
static qboolean
SP_LoadFile(const char *name, sppackage_t *tmp)
{
	char path[MAX_QPATH];
	char *buf;
	int len;

	Com_sprintf(path, sizeof(path), "strip/%s.spe", name);
	len = FS_LoadFile(path, (void **)&buf);

	if (len < 0)
	{
		Com_sprintf(path, sizeof(path), "strip/%s.sp", name);
		len = FS_LoadFile(path, (void **)&buf);
	}

	if (len < 0)
	{
		return false;
	}

	memset(tmp, 0, sizeof(*tmp));

	if (!SP_Parse(buf, len, tmp))
	{
		SP_FreeEntries(tmp->entries);
		memset(tmp, 0, sizeof(*tmp));
		tmp->id = 0;
	}
	else
	{
		SP_FillFromShadowed(path, tmp);
	}

	FS_FreeFile(buf);
	return true;
}

int
SP_Register(const char *name, int owner)
{
	static sppackage_t tmp; /* large; kept off the stack */
	sppackage_t *pkg;

	if (!name || !name[0])
	{
		return -1;
	}

	/* already loaded under this name: add the owner */
	for (pkg = sp_packages; pkg < sp_packages + SP_MAX_PACKAGES; pkg++)
	{
		if (pkg->used && !Q_stricmp(pkg->regname, name))
		{
			pkg->owners |= owner;
			return pkg->id;
		}
	}

	if (!SP_LoadFile(name, &tmp))
	{
		Com_Error(ERR_DROP, "Could not open string package '%s'", name);
		return -1;
	}

	pkg = &sp_packages[tmp.id & 0xFF];

	if (pkg->used)
	{
		/* the first loaded copy wins */
		SP_FreeEntries(tmp.entries);
		pkg->owners |= owner;
		return pkg->id;
	}

	*pkg = tmp;
	pkg->used = true;
	pkg->owners = owner;
	Q_strlcpy(pkg->regname, name, sizeof(pkg->regname));
	memset(&tmp, 0, sizeof(tmp));

	Com_DPrintf("SP_Register: Registered client string package '%s' with ID %02x\n",
			name, pkg->id);

	return pkg->id;
}

static void
SP_FreePackage(sppackage_t *pkg)
{
	SP_FreeEntries(pkg->entries);
	memset(pkg, 0, sizeof(*pkg));
}

void
SP_Unregister(int owner)
{
	int i;

	for (i = 0; i < SP_MAX_PACKAGES; i++)
	{
		sppackage_t *pkg = &sp_packages[i];

		if (!pkg->used)
		{
			continue;
		}

		pkg->owners &= ~owner;

		if (!pkg->owners)
		{
			SP_FreePackage(pkg);
		}
	}
}

const char *
SP_GetStringByID(int id)
{
	int pid = (id >> 8) & 0xFF;
	int slot = id & 0xFF;
	sppackage_t *pkg = &sp_packages[pid];

	if (!pkg->used)
	{
		/* OpenMerc: a single-player map run in deathmatch asks for its level package, which the game registers
		   only in single player (jpn3: 7100). There the text is left empty instead of dropping the level. */
		if (Cvar_VariableValue("deathmatch"))
		{
			static int warned = -1;

			if (warned != pid)
			{
				Com_DPrintf("String package not registered for ID %04x (deathmatch: empty text)\n", id & 0xFFFF);
				warned = pid;
			}

			return "";
		}

		Com_Error(ERR_DROP, "String package not registered for ID %04x", id & 0xFFFF);
		return "";
	}

	return pkg->entries[slot].text ? pkg->entries[slot].text : "";
}

int
SP_GetFlagsByID(int id)
{
	sppackage_t *pkg = &sp_packages[(id >> 8) & 0xFF];

	if (!pkg->used)
	{
		return 0;
	}

	return pkg->entries[id & 0xFF].flags;
}

const char *
SP_GetStringByName(const char *reference)
{
	size_t rl;
	int i, j;

	if (!reference)
	{
		return "";
	}

	rl = strlen(reference);

	for (i = 0; i < SP_MAX_PACKAGES; i++)
	{
		sppackage_t *pkg = &sp_packages[i];
		size_t pl;
		const char *entry;
		const char *text = NULL;

		if (!pkg->used)
		{
			continue;
		}

		pl = strlen(pkg->reference);

		if ((rl < pl + 2) || strncmp(reference, pkg->reference, pl))
		{
			continue;
		}

		/* the separator after the package name is not checked */
		entry = reference + pl + 1;

		/* a later entry with the same name wins */
		for (j = 0; j < SP_MAX_ENTRIES; j++)
		{
			if (pkg->entries[j].name && !strcmp(pkg->entries[j].name, entry))
			{
				text = pkg->entries[j].text ? pkg->entries[j].text : "";
			}
		}

		if (text)
		{
			return text;
		}
	}

	return "";
}

int
SP_CountOwned(int owner)
{
	int i, n = 0;

	for (i = 0; i < SP_MAX_PACKAGES; i++)
	{
		if (sp_packages[i].used && (sp_packages[i].owners & owner))
		{
			n++;
		}
	}

	return n;
}

const char *
SP_NameOwned(int owner, int index)
{
	int i;

	for (i = 0; i < SP_MAX_PACKAGES; i++)
	{
		if (sp_packages[i].used && (sp_packages[i].owners & owner))
		{
			if (index-- == 0)
			{
				return sp_packages[i].regname;
			}
		}
	}

	return NULL;
}

/* every loaded package again, with its owners (sp_language change) */
void
SP_ReloadAll(void)
{
	char names[SP_MAX_PACKAGES][MAX_QPATH];
	int owners[SP_MAX_PACKAGES];
	int i, n = 0;

	for (i = 0; i < SP_MAX_PACKAGES; i++)
	{
		if (sp_packages[i].used)
		{
			Q_strlcpy(names[n], sp_packages[i].regname, MAX_QPATH);
			owners[n] = sp_packages[i].owners;
			n++;
			SP_FreePackage(&sp_packages[i]);
		}
	}

	sp_reloading = true;

	for (i = 0; i < n; i++)
	{
		SP_Register(names[i], owners[i]);
	}

	sp_reloading = false;
}

/* sp_language changed: reload; German forces the locks (SPEC-313 §1) */
static qboolean sp_ready; /* sp_language registered: later calls are sets */

static void
SP_LanguageChanged(cvar_t *var)
{
	if (!sp_ready)
	{
		return; /* the registration itself */
	}

	(void)var;
	SP_ReloadAll();
}

/* MENU2: om_language changed: reload every package */
static void
SP_OmLanguageChanged(cvar_t *var)
{
	(void)var;

	if (sp_ready)
	{
		SP_ReloadAll();
	}
}

/* ---------------------------------------------------------------------
 * OpenMerc diagnostics: om_strings [reference | id]
 * --------------------------------------------------------------------- */

static void
SP_Diag_f(void)
{
	int i;

	if (Cmd_Argc() < 2)
	{
		for (i = 0; i < SP_MAX_PACKAGES; i++)
		{
			sppackage_t *pkg = &sp_packages[i];
			int j, n = 0;

			if (!pkg->used)
			{
				continue;
			}

			for (j = 0; j < SP_MAX_ENTRIES; j++)
			{
				if (pkg->entries[j].text)
				{
					n++;
				}
			}

			Com_Printf("%02x %-20s %-20s owners %i, %i texts\n", pkg->id,
					pkg->regname, pkg->reference, pkg->owners, n);
		}

		return;
	}

	for (i = 1; i < Cmd_Argc(); i++)
	{
		const char *a = Cmd_Argv(i);

		/* +name loads a package with the menus owner, - releases it */
		if (a[0] == '+')
		{
			int id = SP_Register(a + 1, SP_OWNER_MENUS);

			Com_Printf("%s: ID %02x\n", a + 1, id & 0xff);
			continue;
		}

		if (!strcmp(a, "-"))
		{
			SP_Unregister(SP_OWNER_MENUS);
			continue;
		}

		if ((a[0] >= '0') && (a[0] <= '9'))
		{
			int id = (int)strtol(a, NULL, 0);

			if (!sp_packages[(id >> 8) & 0xFF].used)
			{
				Com_Printf("%04x: package not loaded\n", id & 0xFFFF);
				continue;
			}

			Com_Printf("%04x: \"%s\" flags %02x\n", id & 0xFFFF,
					SP_GetStringByID(id), SP_GetFlagsByID(id));
		}
		else
		{
			Com_Printf("%s: \"%s\"\n", a, SP_GetStringByName(a));
		}
	}
}

void
SP_Init(void)
{
	memset(sp_packages, 0, sizeof(sp_packages));

	/* archive + integer, default 0 (SPEC-313 "Corrections") */
	sp_ready = false;
	sp_language = Cvar_GetCB("sp_language", "0", CVAR_ARCHIVE | CVAR_INTEGER,
			SP_LanguageChanged);
	om_language = Cvar_GetCB("om_language", "auto", CVAR_ARCHIVE, SP_OmLanguageChanged);
	sv_show_strip = Cvar_Get("sv_show_strip", "0", 0);
	sp_ready = true;

	Cmd_AddCommand("om_strings", SP_Diag_f);
}

void
SP_Shutdown(void)
{
	int i;

	for (i = 0; i < SP_MAX_PACKAGES; i++)
	{
		if (sp_packages[i].used)
		{
			SP_FreePackage(&sp_packages[i]);
		}
	}
}
