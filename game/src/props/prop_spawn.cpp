// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — spawn dispatch for the prop and server-effect classnames and the level hooks.
// Implements: SPEC-770 §3 (spawn rules), SPEC-771 B.1 (ambient sound init at level spawn), SPEC-760 B1.3 step 5

#include "props.h"
#include "../effects/fx.h"
#include "../effects/ambient.h"
#include "../damage/damage_api.h"

namespace props {

static SpawnTable *s_tables;

SpawnTable::SpawnTable(const SpawnEntry *t) : entries(t), next(s_tables) { s_tables = this; }

void TrashcanReset(void); // prop_level2.cpp
gg::NoteCallback *NoteBanner(void);
gg::NoteCallback *NoteValve(void);
gg::NoteCallback *NoteMeat(void);
gg::NoteCallback *NoteLampHang(void);

// the note callbacks of the props, registered once in a fixed order with G9's savegame callback table
void RegisterNoteCallbacks(void)
{
	gg::NoteCallback *cbs[4] = {NoteBanner(), NoteValve(), NoteMeat(), NoteLampHang()};
	for (gg::NoteCallback *c : cbs)
		if (c && !c->registered) {
			GG_RegisterCallback(c);
			c->registered = true;
		}
}

void LevelInit(void)
{
	g3hooks.musicDanger = ambient::MusicDanger; // SPEC-771 B.5 danger trigger raised by the damage code
	RegisterNoteCallbacks();
	fx::LevelReset();
	ResetAllData();
	ResetSoundList();
	TrashcanReset();
	ambient::LevelReset();
}

void LevelFinish(void)
{
	FlushSoundList();
	ambient::LevelSpawned();
}

static void (*Find(const char *classname))(edict_t *)
{
	for (SpawnTable *t = s_tables; t; t = t->next)
		for (const SpawnEntry *e = t->entries; e->classname; e++)
			if (!strcmp(e->classname, classname))
				return e->spawn;
	return NULL;
}

} // namespace props

bool G7_CallSpawn(edict_t *ent)
{
	void (*fn)(edict_t *) = props::Find(ent->classname);
	if (!fn)
		return false;
	if (!level.g7_levelinit) {
		props::LevelInit();
		level.g7_levelinit = 1;
	}
	props::ClearData(ent);
	// spawn-time keys `material` / `surfacetype` share storage with the class choices (SPEC-770 §3 step 4, §6B.1)
	ent->material = st.material;
	ent->surfacetype = st.surfacetype;
	fn(ent);
	st.material = ent->material;
	st.surfacetype = ent->surfacetype;
	return true;
}

void G7_LevelSpawned(void)
{
	if (!level.g7_levelinit) {
		props::LevelInit();
		level.g7_levelinit = 1;
	}
	props::LevelFinish();
}
