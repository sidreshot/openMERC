/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-741 4 (personality list, .bpf parse rules, choice for a new bot), SPEC-133 (.bpf keys)
 * Owner: package G5 (game/src/bots).
 */
#include "bot_local.h"

std::vector<BotPersonality> g_bot_pers;
static int g_list_cursor;  /* bot_random 0: list order, survives level changes */
static int g_round;        /* bot_random != 0: current round of the least-used rule (per level) */

static int LoadText(const char *path, std::string &out, size_t limit)
{
	void *buf = NULL;
	int len = gix.FS_LoadFile(path, &buf, 0);
	if (len <= 0 || !buf) {
		if (buf)
			gix.FS_FreeFile(buf);
		return 0;
	}
	size_t n = (size_t)len < limit ? (size_t)len : limit;
	out.assign((const char *)buf, n);
	gix.FS_FreeFile(buf);
	return 1;
}

/* every '#' starts a comment running up to and including the next line feed */
static std::string StripComments(const std::string &in)
{
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); i++) {
		if (in[i] == '#') {
			while (i < in.size() && in[i] != '\n')
				i++;
			continue; /* the line feed itself is swallowed */
		}
		out += in[i];
	}
	return out;
}

/* numeric prefix of the text after `key` up to the line feed, truncated towards zero; missing key -> 0 */
static int NumberKey(const std::string &t, const char *key, int def, bool *found = NULL)
{
	size_t p = t.find(key);
	if (found)
		*found = p != std::string::npos;
	if (p == std::string::npos)
		return def;
	p += strlen(key);
	size_t e = t.find('\n', p);
	std::string v = t.substr(p, e == std::string::npos ? std::string::npos : e - p);
	return (int)atof(v.c_str());
}

static bool ParseBpf(const char *name, BotPersonality &bp)
{
	std::string raw;
	char path[MAX_QPATH * 2];
	Com_sprintf(path, sizeof(path), "bot/bots/%s.bpf", name);
	if (!LoadText(path, raw, 65536))
		return false;
	std::string t = StripComments(raw);
	memset(bp.userinfo, 0, sizeof(bp.userinfo));
	size_t p = t.find("userinfo=");
	if (p != std::string::npos) {
		p += 9;
		size_t e = t.find('\r', p);
		std::string v = t.substr(p, e == std::string::npos ? std::string::npos : e - p);
		strncpy(bp.userinfo, v.c_str(), 512);
	}
	/* a missing mandatory key made the original read invalid memory; treated as 0 here */
	bp.accuracy = NumberKey(t, "accuracy=", 0);
	bp.fov = NumberKey(t, "fov=", 0);
	bp.turning_speed = NumberKey(t, "turning_speed=", 0);
	bp.view_distance = NumberKey(t, "view_distance=", 0);
	bp.intelligence = NumberKey(t, "general_intelligence=", 0);
	bp.canchat = NumberKey(t, "canchat=", 0);
	bp.chatfreq = NumberKey(t, "chatfreq=", 20);
	size_t from = 0;
	for (int n = 1; n <= 6; n++) {
		char a[24], b[24];
		Com_sprintf(a, sizeof(a), "[SECT=%d]", n);
		Com_sprintf(b, sizeof(b), "[SECTEND=%d]", n);
		size_t s = t.find(a, from);
		if (s == std::string::npos)
			break;
		s += strlen(a);
		size_t e = t.find(b, s);
		if (e == std::string::npos)
			break;
		from = e + strlen(b);
		/* lines start after the marker's own line end */
		size_t q = t.find('\n', s);
		if (q == std::string::npos || q >= e)
			continue;
		q++;
		while (q < e) {
			size_t le = t.find('\n', q);
			if (le == std::string::npos || le >= e)
				break; /* the line holding [SECTEND=n] */
			std::string line = t.substr(q, le - q);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			bp.sect[n].push_back(line);
			q = le + 1;
		}
	}
	return true;
}

void BotPers_Free(void) { g_bot_pers.clear(); }

void BotPers_Load(void)
{
	BotPers_Free();
	std::string list;
	char path[MAX_QPATH * 2];
	Com_sprintf(path, sizeof(path), "bot/%s", bot_listfile ? bot_listfile->string : "botlist.txt");
	if (!LoadText(path, list, (size_t)-1))
		return;
	size_t p = 0;
	while (p < list.size()) {
		size_t e = list.find('\n', p);
		std::string line = list.substr(p, e == std::string::npos ? std::string::npos : e - p);
		p = e == std::string::npos ? list.size() : e + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		if (line.size() > 63)
			line.resize(63);
		BotPersonality bp;
		memset(bp.name, 0, sizeof(bp.name));
		strncpy(bp.name, line.c_str(), 63);
		bp.uses = 0;
		bp.valid = ParseBpf(bp.name, bp);
		if (bp.valid)
			g_bot_pers.push_back(bp);
	}
}

void BotPers_NewLevel(void)
{
	for (auto &bp : g_bot_pers)
		bp.uses = 0;
	g_round = 0;
}

int BotPers_Find(const char *name)
{
	for (size_t i = 0; i < g_bot_pers.size(); i++)
		if (!Q_stricmp(g_bot_pers[i].name, name))
			return (int)i;
	return 0;
}

int BotPers_Choose(bool count_use)
{
	int n = (int)g_bot_pers.size();
	if (n <= 0)
		return -1;
	int r;
	if (!bot_random || bot_random->value == 0) {
		r = g_list_cursor % n;
		g_list_cursor = (g_list_cursor + 1) % n;
	} else {
		r = gix.irand(0, n - 1);
		int step = n, probes = 0;
		while (g_bot_pers[r].uses > g_round) {
			r = (r + step / 2) % n;
			if (step > 1)
				step /= 2;
			if (++probes >= n) {
				g_round++;
				probes = 0;
				r = gix.irand(0, step - 1);
			}
		}
	}
	if (count_use)
		g_bot_pers[r].uses++;
	return r;
}
