// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - string-package ids by entry name. The game sends messages by 16-bit id
// (package ID << 8 | entry slot); the ids are read from the shipped package files themselves
// (strip/<name>.sp, keywords VERSION/ID/COUNT/INDEX/REFERENCE) so no table is hard-coded.
// Implements: SPEC-313 1 (package format at run time), SPEC-001 #48/#49 (string ids)

#include "g_local.h"

struct SPEntry {
	char name[48];
	int id;
};
struct SPPackage {
	char name[32];
	int id;
	int count;
	SPEntry *entries;
};

#define MAX_SP 16
static SPPackage g_sp[MAX_SP];
static int g_numsp;

static SPPackage *SP_Load(const char *package)
{
	for (int i = 0; i < g_numsp; i++)
		if (!Q_stricmp(g_sp[i].name, package))
			return &g_sp[i];
	if (g_numsp >= MAX_SP)
		return NULL;
	SPPackage *p = &g_sp[g_numsp++];
	memset(p, 0, sizeof(*p));
	strncpy(p->name, package, sizeof(p->name) - 1);
	p->id = -1;

	void *buf = NULL;
	char path[MAX_QPATH];
	Com_sprintf(path, sizeof(path), "strip/%s.sp", package);
	int len = gix.FS_LoadFile(path, &buf, 0);
	if (len <= 0 || !buf)
		return p;
	p->entries = (SPEntry *)gi.TagMalloc(sizeof(SPEntry) * 256, TAG_GAME);
	const char *s = (const char *)buf, *end = s + len;
	int index = -1, depth = 0;
	while (s < end) {
		const char *eol = s;
		while (eol < end && *eol != '\n')
			eol++;
		char line[512];
		int n = (int)(eol - s);
		if (n > 511)
			n = 511;
		memcpy(line, s, n);
		line[n] = 0;
		s = eol + 1;
		char *w = line;
		while (*w == ' ' || *w == '\t')
			w++;
		char *e = w + strlen(w);
		while (e > w && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
			*--e = 0;
		if (!strncmp(w, "ID ", 3) && depth == 0)
			p->id = atoi(w + 3) & 0xFF;
		else if (!strncmp(w, "INDEX ", 6))
			index = atoi(w + 6);
		else if (!strcmp(w, "{"))
			depth++;
		else if (!strcmp(w, "}"))
			depth--;
		else if (!strncmp(w, "REFERENCE ", 10) && depth == 1 && index >= 0 && index < 256 && p->count < 256) {
			SPEntry *en = &p->entries[p->count++];
			strncpy(en->name, w + 10, sizeof(en->name) - 1);
			en->id = index;
		}
	}
	gix.FS_FreeFile(buf);
	if (p->id < 0)
		p->id = 0;
	return p;
}

int SP_Id(const char *package, const char *entry)
{
	SPPackage *p = SP_Load(package);
	if (!p || !p->entries)
		return -1;
	int found = -1;
	for (int i = 0; i < p->count; i++)
		if (!strcmp(p->entries[i].name, entry))
			found = p->entries[i].id; // the later entry wins (SPEC-313 1)
	if (found < 0) {
		gi.dprintf("G18: string %s_%s not found\n", package, entry);
		return -1;
	}
	return (p->id << 8) | found;
}

// prints that are skipped silently when the id is unknown
void SP_PrintTo(edict_t *ent, const char *package, const char *entry)
{
	int id = SP_Id(package, entry);
	if (id >= 0)
		gix.SP_Print(ent, id);
}
