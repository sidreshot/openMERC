// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - level-spawn marks of the material sound list (the 115-entry list of SPEC-AMEND-005 G18.27,
// held by G7's props::MarkSoundList and precached in list order at ambient sound init): material groups of the
// map's surface materials, the always-marked sparks and metal sounds, the break sounds of the spawned entities'
// surface types and Player/Land/Water. Spawn-function marks of props and helicopters are G7's/G4's (rule 4).
// Implements: SPEC-760 B1.3 step 3/5, SPEC-AMEND-005 G18.27 (sound rules 1, 2, 3, 5), SPEC-001 import #26

#include "g_local.h"
#include "props/props.h" /* G7: shared client sound list marks */

// client sound list bases (SPEC-401)
enum { FOOT_METAL = 2, FOOT_SNOW = 6, FOOT_WATER = 10, FOOT_GRASS = 14, FOOT_GRAVEL = 18, FOOT_SAND = 22,
	FOOT_WOOD = 26, FOOT_STONE = 30 };
enum { IMP_STONE = 37, IMP_WATER = 38, IMP_WOOD = 39, IMP_GRASS = 40, IMP_GRAVEL = 41, IMP_SAND = 42, IMP_SNOW = 43,
	IMP_METAL = 79 /* 79..81 */ };
enum { LAND_GRASS = 71, LAND_GRAVEL = 72, LAND_METAL = 73, LAND_SAND = 74, LAND_SNOW = 75, LAND_STONE = 76,
	LAND_WATER = 77, LAND_WOOD = 78 };
enum { SPARK1 = 0, SPARK2 = 1, GLASS_BIG = 82, GLASS_MED = 83, GLASS_SMALL = 84, BREAK_METAL = 85, BREAK_STONE = 86,
	BREAK_WOOD = 87, EXPLOSION_SMALL = 91 };

static void MarkRange(int first, int n)
{
	for (int i = 0; i < n; i++)
		props::MarkSoundList(first + i);
}

// rule 1: material id of a surface of the map → footsteps, surface impact and landing sounds
static void MarkMaterial(int m)
{
	if (m == 0 || (m >= 17 && m <= 24) || (m >= 33 && m <= 35)) {
		MarkRange(FOOT_STONE, 4), props::MarkSoundList(IMP_STONE), props::MarkSoundList(LAND_STONE);
	} else if (m >= 2 && m <= 5) {
		MarkRange(FOOT_SAND, 4), props::MarkSoundList(IMP_SAND), props::MarkSoundList(LAND_SAND);
	} else if (m >= 6 && m <= 8) {
		MarkRange(FOOT_GRAVEL, 4), props::MarkSoundList(IMP_GRAVEL), props::MarkSoundList(LAND_GRAVEL);
	} else if (m == 9) {
		MarkRange(FOOT_SNOW, 4), props::MarkSoundList(IMP_SNOW), props::MarkSoundList(LAND_SNOW);
	} else if ((m >= 10 && m <= 13) || m == 27 || m == 36) {
		MarkRange(FOOT_WATER, 4), props::MarkSoundList(IMP_WATER), props::MarkSoundList(LAND_WATER);
	} else if ((m >= 14 && m <= 16) || m == 40) {
		MarkRange(FOOT_WOOD, 4), props::MarkSoundList(IMP_WOOD), props::MarkSoundList(LAND_WOOD);
	} else if (m == 25 || m == 26) {
		MarkRange(FOOT_GRASS, 4), props::MarkSoundList(IMP_GRASS), props::MarkSoundList(LAND_GRASS);
	} else if ((m >= 28 && m <= 32) || (m >= 37 && m <= 39) || m == 41 || m == 42) {
		MarkRange(FOOT_METAL, 4), props::MarkSoundList(LAND_METAL);
		if (m == 32)
			props::MarkSoundList(EXPLOSION_SMALL);
		if (m == 37 || m == 38)
			props::MarkSoundList(GLASS_SMALL);
	}
	// 1, 43 and above: nothing
}

// rule 3: the break sounds by the surface type of every spawned entity, the world included (surface type 0), so
// Impact/Break/Stone is always marked. Collected while the entities spawn and marked after the last one (the G7 list
// is reset when the first prop spawns).
static bool debris_mark[114];

void G_DebrisReset(void) { memset(debris_mark, 0, sizeof(debris_mark)); }

void G_DebrisSurface(int surfacetype)
{
	int m = surfacetype;
	if (m >= 14 && m <= 16)
		debris_mark[BREAK_WOOD] = true;
	else if ((m >= 28 && m <= 31) || m == 42)
		debris_mark[BREAK_METAL] = true;
	else if (m == 32)
		debris_mark[BREAK_METAL] = debris_mark[SPARK1] = debris_mark[EXPLOSION_SMALL] = true;
	else if (m == 37 || m == 38)
		debris_mark[GLASS_BIG] = debris_mark[GLASS_MED] = debris_mark[GLASS_SMALL] = true;
	else if (m == 0 || (m >= 2 && m <= 13) || (m >= 17 && m <= 27) || (m >= 33 && m <= 36) || (m >= 39 && m <= 41))
		debris_mark[BREAK_STONE] = true;
	// 1, 43 and above: nothing
}

void G_MarkSurfaceSounds(void)
{
	// the G7 list is reset by its level init (first prop of the level or G7_LevelSpawned); make sure that
	// happened before marking, exactly as G7_LevelSpawned does
	if (!level.g7_levelinit) {
		props::LevelInit();
		level.g7_levelinit = 1;
	}
	// rule 1: the map's material list through the engine (SPEC-001 import #26, "the distinct materials of the map's
	// surfaces"; the engine's list, not the texinfo bytes: dm/plantdm's has 32 although no texinfo carries it)
	uint8_t mats[256];
	int n = gix.getmaterials ? gix.getmaterials(mats, sizeof(mats)) : 0;
	for (int i = 0; i < n && i < (int)sizeof(mats); i++)
		MarkMaterial(mats[i]);
	props::MarkSoundList(SPARK1); // rule 1: the sparks unconditionally
	props::MarkSoundList(SPARK2);
	MarkRange(FOOT_METAL, 4); // rule 2: always
	props::MarkSoundList(LAND_METAL);
	MarkRange(IMP_METAL, 3);
	for (int k = 0; k < 114; k++) // rule 3
		if (debris_mark[k])
			props::MarkSoundList(k);
	props::MarkSoundList(LAND_WATER); // rule 5
}
