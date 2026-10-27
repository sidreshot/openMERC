/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-741 12 (chat: triggers, typing delay, name substitution, replies, the 1.07f section off-by-one)
 * Owner: package G5 (game/src/bots).
 */
#include "bot_local.h"

#include <ctype.h>

static bool ChatAllowed(Bot *b) { return bot_chat && bot_chat->value != 0 && BotP(b).canchat != 0; }

static const char *NameOf(edict_t *x) { return x && x->client ? x->client->pers.netname : ""; }

/* prepare chat index k (1..6) of bot b towards client x; index k uses file section k + 1 (index 6 has none) */
static bool Prepare(Bot *b, edict_t *x, int k)
{
	const BotPersonality &P = BotP(b);
	int sect = k + 1;
	if (sect < 1 || sect > 6 || P.sect[sect].empty())
		return false;
	const std::string &line = P.sect[sect][gix.irand(0, (int)P.sect[sect].size() - 1)];
	const char *name = NameOf(x);
	char out[257];
	int n = 0;
	for (char c : line) {
		if (n >= 256)
			break;
		if (c == '%' || c == '$' || c == '&') {
			for (const char *p = name; *p && n < 256; p++)
				out[n++] = c == '$' ? (char)toupper((unsigned char)*p) : c == '&' ? (char)tolower((unsigned char)*p) : *p;
		} else
			out[n++] = c;
	}
	out[n] = 0;
	BotChatLine &cl = b->chat;
	BotStrCpy(cl.text, out, sizeof(cl.text));
	cl.index = k;
	cl.pending = true;
	cl.team = 0;
	float delay = (float)((n - 1) / 5);
	if (delay < 2.5f)
		delay = 2.5f;
	cl.say_time = level.time + delay;
	b->chat_target = x;
	return true;
}

void BotChat_Trigger(Bot *b, edict_t *x, int index)
{
	if (!ChatAllowed(b) || b->chat.pending || !x)
		return;
	if (gix.flrand(0, 100) > BotP(b).chatfreq)
		return;
	if (OnSameTeam(g_edicts + b->clientnum, x))
		return;
	Prepare(b, x, index);
}

void BotChat_Idle(Bot *b, edict_t *ent)
{
	if (level.time < b->idle_chat_time)
		return;
	b->idle_chat_time = level.time + gix.flrand(75, 100);
	if (!ChatAllowed(b) || b->chat.pending)
		return;
	int inuse = 0;
	for (int i = 1; i <= game.maxclients; i++)
		if (g_edicts[i].inuse && g_edicts[i].client)
			inuse++;
	if (!inuse)
		return;
	int k = (int)gix.flrand(1, (float)inuse);
	edict_t *x = NULL;
	for (int i = 1, c = 0; i <= game.maxclients; i++)
		if (g_edicts[i].inuse && g_edicts[i].client && ++c == k) {
			x = g_edicts + i;
			break;
		}
	if (!x)
		return;
	(void)ent;
	BotChat_Trigger(b, x, 3);
}

static void Say(Bot *b)
{
	edict_t *ent = g_edicts + b->clientnum;
	const char *name = NameOf(ent);
	bool teamonly = b->chat.team && ((int)dmflags->value & 0x80);
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *o = g_edicts + i;
		if (!o->inuse || !o->client || BotFor(o))
			continue;
		if (teamonly && !OnSameTeam(ent, o))
			continue;
		gix.clprintf(o, ent, teamonly ? 1 : 0, "%s: %s\n", name, b->chat.text);
	}
	if (dedicated && dedicated->value)
		gi.dprintf("%s: %s\n", name, b->chat.text);
}

static void Reply(Bot *speaker)
{
	Bot *x = BotFor(speaker->chat_target);
	if (!x || x == speaker || !ChatAllowed(x))
		return;
	static const int map[7] = {0, 5, 6, 4, 0, 0, 0};
	int k = speaker->chat.index >= 1 && speaker->chat.index <= 6 ? map[speaker->chat.index] : 0;
	if (!k)
		return;
	if (x->chat.pending && x->chat.index >= 5)
		return;
	int cf = BotP(x).chatfreq;
	int chance = (int)(cf * 2.5f);
	if (chance > 90 && cf < 90)
		chance = 90;
	if (gix.flrand(0, 100) > chance)
		return;
	x->chat.pending = false;
	Prepare(x, g_edicts + speaker->clientnum, k);
}

void BotChat_Frame(void)
{
	for (int i = 1; i <= game.maxclients; i++) {
		Bot *b = &g_bots[i];
		if (!b->inuse || !b->chat.pending || level.time < b->chat.say_time)
			continue;
		b->chat.pending = false;
		Say(b);
		Reply(b);
	}
}

void Bot_HeardChat(edict_t *speaker, const char *text)
{
	/* replies are tied to the index of a bot's own line (SPEC-741 12); a human's text triggers nothing */
	(void)speaker;
	(void)text;
}

void BotChat_Announce(Bot *b, edict_t *ent, int objective)
{
	(void)b;
	(void)ent;
	(void)objective;
}
