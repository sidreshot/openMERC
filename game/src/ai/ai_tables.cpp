/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-700 ("Character type codes", "Combat profile", "Constants": personality rows, profile tiers,
 * profile weapon classes, per-type profile table, level identification order), SPEC-760 B9.2 (toughness rows used
 * by the AI), SPEC-731 amendment 4 (vehicle teams)
 * The rows are small gameplay parameters (teams, personalities, profile tiers, toughness) tuned to the observed
 * behaviour of the characters; the class names are those used by the maps in the game data.
 * Owner: package G2.
 */
#include "ai_local.h"

namespace om_ai {

/* ------------------------------------------------------------------------------------------------------------------
 * type codes
 * ---------------------------------------------------------------------------------------------------------------- */
struct ClassCode {
	const char *classname;
	int code;
};

static const ClassCode class_codes[] = {
	{"m_x_bull", 6},
	{"m_x_chopper_black", 8}, {"m_x_chopper_green", 8}, {"m_x_chopper_white", 8}, {"m_x_hind", 8},
	{"m_x_tank", 9}, {"m_x_snowcat", 10},
	{"m_x_husky", 11}, {"m_x_germanshep", 11}, {"m_x_raiderdog", 11}, {"m_x_rottweiler", 12},
	{"m_nyc_eskinhead3", 15}, {"m_nyc_estockbroker", 16}, {"m_nyc_etourist", 17}, {"m_nyc_ebum", 18},
	{"m_irq_eman1", 21}, {"m_afr_ecommander", 22}, {"m_kos_eofficer", 23}, {"m_kos_erefugee", 24},
	{"m_irq_eofficer", 25}, {"m_afr_eworker", 26}, {"m_sib_eofficer", 28}, {"m_sib_escientist1", 29},
	{"m_nyc_epunk", 30},
	{"m_x_mcharacter", 31}, {"m_x_mcharacter_snow", 32}, {"m_x_mcharacter_desert", 33}, {"m_x_mmerc", 34},
	{"m_x_mhurtmerc", 35}, {"m_x_msam", 36}, {"m_irq_moilworker", 37}, {"m_nyc_mpunk", 38},
	{"m_x_miraqboss", 39}, {"m_x_mraiderboss1", 40}, {"m_x_mraiderboss2", 41},
	{"m_irq_msoldier1", 42}, {"m_irq_msoldier2a", 43}, {"m_irq_msoldier2b", 44}, {"m_irq_mrepguard1a", 45},
	{"m_irq_mrepguard1b", 46}, {"m_irq_mpolice", 47}, {"m_irq_mcommander", 48}, {"m_irq_mbrute1a", 49},
	{"m_irq_mbrute1b", 50}, {"m_irq_mbodyguard", 51}, {"m_irq_mrocket", 52}, {"m_irq_msaddam", 53},
	{"m_irq_mman2", 54},
	{"m_x_romulan", 55}, {"m_afr_msniper", 55},
	{"m_afr_msoldier1a", 56}, {"m_afr_msoldier1b", 57}, {"m_afr_msoldier2", 58}, {"m_afr_msoldier3", 59},
	{"m_nyc_mswat", 60}, {"m_nyc_mswatleader", 61},
	{"m_cas_mraider1", 62}, {"m_cas_mraider2a", 63}, {"m_cas_mraider2b", 64}, {"m_cas_mbrute", 65},
	{"m_cas_mrocket", 66},
	{"m_sib_mtrooper2", 67}, {"m_sib_mcleansuit", 68},
	{"m_kos_mgrunt1", 69}, {"m_kos_mgrunt2", 70}, {"m_kos_mgrunt3", 71}, {"m_kos_msniper1a", 72},
	{"m_kos_msniper1b", 73}, {"m_kos_mcomtroop", 74}, {"m_kos_mbrute1a", 75}, {"m_kos_mbrute1b", 76},
	{"m_kos_mmechanic", 77}, {"m_kos_mrebel", 78}, {"m_kos_mklaleader", 79},
	{"m_nyc_mskinhead1", 80}, {"m_nyc_mskinhead2a", 81}, {"m_nyc_mskinhead2b", 82}, {"m_x_mskinboss", 83},
	{"m_nyc_mpolitician", 84}, {"m_tok_mman2", 85},
	{"m_tok_mhench1", 86}, {"m_tok_mhench2", 87}, {"m_tok_mkiller", 88}, {"m_tok_mninja", 89},
	{"m_tok_mbrute", 90}, {"m_tok_mman1", 91}, {"m_afr_mbrute", 92}, {"m_afr_mrocket", 93},
	{"m_sib_mtrooper1a", 94}, {"m_sib_mtrooper1b", 95}, {"m_sib_mguard1", 96}, {"m_sib_mguard3", 97},
	{"m_sib_mguard4", 98}, {"m_sib_mmechanic", 99},
	{"m_nyc_fskinchick", 100}, {"m_nyc_fwoman", 101}, {"m_sib_fguard2", 102}, {"m_sib_fscientist2", 103},
	{"m_irq_fwoman1", 104}, {"m_irq_fwoman2", 105}, {"m_tok_fwoman1", 106}, {"m_tok_fwoman2", 107},
	{"m_tok_fassassin", 108}, {"m_cas_ffemale", 109}, {"m_x_ftaylor", 110},
	{nullptr, 0}};

int TypeCodeForClassname(const char *classname)
{
	if (!classname)
		return 0;
	for (const ClassCode *c = class_codes; c->classname; c++)
		if (!strcmp(c->classname, classname))
			return c->code;
	return 0;
}

/* team per code (SPEC-700 table; 9 and 10: team 1, ERRATA SPEC-731 amendment 4) */
static int TeamOf(int code)
{
	switch (code) {
	case 6: return 100;
	case 8: return 10;
	case 15: case 30: case 38: case 80: case 81: case 82: case 83: case 100: return 3;
	case 16: case 17: case 18: case 19: case 20: case 24: case 31: case 32: case 33: case 34: case 35: case 36:
	case 60: case 61: case 78: case 79: case 84: case 85: case 91: case 101: case 106: case 107: case 110:
		return 0;
	default: return 1;
	}
}

static bool InnocentOf(int code)
{
	switch (code) {
	case 18: case 21: case 29: case 37: case 54: case 77: case 85: case 99: case 104: case 105: return true;
	default: return false;
	}
}

static Family FamilyOf(int code)
{
	if (code == 6 || code == 13 || code == 14)
		return FAMILY_SPECIAL;
	if (code == 8)
		return FAMILY_HELICOPTER;
	if (code == 9 || code == 10)
		return FAMILY_VEHICLE;
	if (code == 11 || code == 12)
		return FAMILY_DOG;
	if (code >= 15 && code <= 30)
		return FAMILY_CIVILIAN;
	if (code >= 31 && code <= 99)
		return FAMILY_MALE;
	if (code >= 100 && code <= 110)
		return FAMILY_FEMALE;
	return FAMILY_NONE;
}

bool GetTypeInfo(int code, TypeInfo *out)
{
	if (code < TYPE_BULL || code > TYPE_LAST)
		return false;
	out->team = TeamOf(code);
	out->innocent = InnocentOf(code);
	out->family = FamilyOf(code);
	return true;
}

/* ------------------------------------------------------------------------------------------------------------------
 * combat profiles (SPEC-700 rule 19 and Constants)
 * ---------------------------------------------------------------------------------------------------------------- */
const int personality_rows[6][6] = {
	{2, 0, 1, 2, 2, 3}, {0, 0, 0, 0, 0, 2}, {1, 3, 4, 0, 1, 2}, {2, 1, 2, 0, 3, 2}, {1, 1, 3, 1, 1, 2},
	{0, 2, 3, 1, 1, 2}};

struct Tier {
	float p3, p4, p5, p6;
	int p7;
};
static const Tier tiers[7] = {{0.2f, 1.0f, 6.0f, 20, 0},  {0.25f, 1.0f, 5.0f, 40, 0}, {0.4f, 1.0f, 3.0f, 120, 0},
                              {0.35f, 1.0f, 4.0f, 80, 0}, {0.35f, 1.0f, 4.0f, 120, 0}, {0.4f, 0.5f, 3.5f, 140, 0},
                              {0.4f, 0.5f, 3.0f, 200, 5}};

static void ApplyTier(float *p, int tier)
{
	if (tier < 0 || tier > 6)
		return;
	p[3] = tiers[tier].p3;
	p[4] = tiers[tier].p4;
	p[5] = tiers[tier].p5;
	p[6] = tiers[tier].p6;
	p[7] = (float)tiers[tier].p7;
	p[8] = 1.0f;
}

static void ApplyWeaponClass(float *p, int wc)
{
	int flags = (int)p[7];
	switch (wc) {
	case 3: p[0] = 384; p[1] = 768; p[2] = 1; p[5] *= 0.5f; break;
	case 4: p[0] = 384; p[1] = 768; p[2] = 1; p[5] *= 0.5f; p[6] *= 1.5f; p[3] *= 0.9f; break;
	case 5: p[0] = 128; p[1] = 256; p[2] = 1; p[4] *= 0; p[5] *= 2; p[6] *= 2; p[3] *= 0.35f; break;
	case 6: p[0] = 192; p[1] = 448; p[2] = 1; flags |= 16; p[4] *= 0; p[5] *= 2; p[6] *= 2.5f; p[3] *= 0.35f; break;
	case 7: p[0] = 512; p[1] = 1024; p[2] = 0; flags |= 10; p[6] *= 2; p[3] *= 3; break;
	case 9: p[0] = 256; p[1] = 512; p[2] = 1; p[6] *= 4; break;
	case 11: p[0] = 96; p[1] = 192; p[2] = 0; p[6] *= 2; p[3] *= 0.3f; break;
	case 12: p[0] = 256; p[1] = 512; p[2] = 1; p[5] *= 1.5f; p[6] *= 4; p[3] *= 0.35f; break;
	case 14: p[0] = 256; p[1] = 768; p[2] = 0; flags |= 21; p[6] *= 7; break;
	case 16: p[0] = 256; p[1] = 512; p[2] = 1; p[6] *= 7; p[3] *= 0.4f; break;
	case 18:
		p[0] = 64; p[1] = 160; p[2] = 0; flags |= 144; p[4] *= 0; p[5] *= 0; p[6] *= 3; p[8] *= 2; p[3] *= 5;
		break;
	default: return;
	}
	p[7] = (float)flags;
}

struct Built {
	int tier, wclass;
	int codes[8];
};
static const Built built[] = {
	{0, 4, {23, 24, 74}}, {0, 6, {70, 71, 75, 76, 78, 79}}, {0, 7, {72, 73}}, {0, 11, {69}},
	{1, 3, {28, 68}}, {1, 4, {27}}, {1, 5, {67, 97, 102}}, {1, 6, {94, 96, 98}}, {1, 7, {95}},
	{2, 4, {25, 26, 47, 48}}, {2, 6, {43, 45, 49}}, {2, 7, {42, 51}}, {2, 12, {44, 46}}, {2, 14, {52}},
	{2, 18, {50}},
	{3, 3, {15, 30, 60, 61, 81, 100}}, {3, 4, {83}}, {3, 6, {82}}, {3, 11, {80}},
	{4, 3, {56}}, {4, 4, {22, 92}}, {4, 7, {55}}, {4, 11, {57, 58}}, {4, 14, {93}}, {4, 18, {59}},
	{5, 3, {86, 108}}, {5, 5, {89}}, {5, 6, {87, 90}}, {5, 16, {88}},
	{6, 5, {63}}, {6, 9, {64}}, {6, 12, {62}}, {6, 14, {66}}, {6, 16, {65}}, {6, 18, {109}},
};

struct Explicit {
	float v[9];
	int codes[16];
};
static const Explicit explicit_entries[] = {
	{{384, 512, 0.3f, 1.0f, 0.5f, 3.0f, 25, 0, 2.0f}, {16, 17, 18, 19, 20, 21, 29}},
	{{192, 512, 0.3f, 1.0f, 0.5f, 3.0f, 25, 0, 2.0f}, {31, 32, 33, 34, 35, 36, 37, 39, 53, 54, 103, 104, 105, 106, 107}},
	{{280, 512, 0.3f, 1.0f, 0.5f, 3.0f, 25, 0, 2.0f}, {77, 84, 85, 91, 99, 101, 110}},
	{{48, 128, 0, 0.25f, 1.0f, 2.0f, 96, 16, 0.6f}, {38}},
	{{192, 512, 0.7f, 1.0f, 0.38f, 1.0f, 275, 5, 1.5f}, {40}},
	{{192, 384, 0.28f, 0.5f, 0.75f, 1.0f, 2750, 37, 0.6f}, {41}},
};

static void ProfileDefaults(float p[AI::NUM_PROFILE])
{
	static const float def[9] = {100, 300, 0.8f, 1.0f, 0, 0, 0, 0, 1.0f};
	for (int i = 0; i < AI::NUM_PROFILE; i++)
		p[i] = i < 9 ? def[i] : 0;
}

void BuildProfile(int code, float p[AI::NUM_PROFILE], bool *has_entry)
{
	ProfileDefaults(p);
	*has_entry = false;
	for (const Explicit &e : explicit_entries)
		for (int c : e.codes)
			if (c && c == code) {
				for (int i = 0; i < 9; i++)
					p[i] = e.v[i];
				*has_entry = true;
				return;
			}
	for (const Built &b : built)
		for (int c : b.codes)
			if (c && c == code) {
				ApplyTier(p, b.tier);
				ApplyWeaponClass(p, b.wclass);
				*has_entry = true;
				return;
			}
}

/* ------------------------------------------------------------------------------------------------------------------
 * level identification (SPEC-700 rule 20)
 * ---------------------------------------------------------------------------------------------------------------- */
struct LevelId {
	const char *name;
	int number;
};
static const LevelId level_ids[] = {
	{"tut1", 1}, {"tsr1", 2}, {"tsr2", 3}, {"arm1", 30}, {"arm2", 31}, {"arm3", 32}, {"trn1", 4}, {"sib2", 9},
	{"kos1", 5}, {"kos2", 6}, {"kos3", 7}, {"sib1", 8}, {"sib3", 10}, {"irq1a", 11}, {"irq2a", 12},
	{"irq3a", 13}, {"nyc1", 14}, {"nyc2", 15}, {"nyc3", 16}, {"sud1", 17}, {"sud2", 18}, {"sud3", 19},
	{"jpn1", 20}, {"jpn2", 21}, {"jpn3", 22}, {"irq1b", 23}, {"irq2b", 24}, {"irq3b", 25}, {"ger1", 26},
	{"ger2", 27}, {"ger3", 28}, {"ger4", 29}, {nullptr, 0}};

static bool LevelMatches(const char *name)
{
	if (!Q_stricmp(level.mapname, name))
		return true;
	if (strstr(level.mapname, name))
		return true;
	if (level.ainame[0] && !Q_stricmp(level.ainame, name))
		return true;
	return false;
}

int LevelNumber(void)
{
	for (const LevelId *l = level_ids; l->name; l++)
		if (LevelMatches(l->name))
			return l->number;
	return 0;
}

const char *LevelName(int number)
{
	if (number == 0 || number == 2)
		return "TSR1";
	if (number == 1 || number > 32)
		return "tut1";
	for (const LevelId *l = level_ids; l->name; l++)
		if (l->number == number)
			return l->name;
	return "tut1";
}

/* ------------------------------------------------------------------------------------------------------------------
 * skill parameters (SPEC-760 B9.2 toughness rows; meanings SPEC-710)
 * ---------------------------------------------------------------------------------------------------------------- */
static const float toughness_rows[5][9] = {
	{0.3f, 0.25f, 0.4f, 4.0f, 0.5f, 0.5f, 0.1f, 2.0f, 1},
	{0.5f, 0.4f, 0.5f, 2.0f, 1.0f, 0.5f, 0.2f, 1.0f, 1},
	{1.0f, 1.0f, 0.8f, 1.0f, 1.0f, 1.0f, 0.3f, 1.0f, 0},
	{3.0f, 1.5f, 1.0f, 0.5f, 1.0f, 1.5f, 0.5f, 1.0f, 0},
	{99, 2.0f, 2.5f, 0.3f, 99, 3.0f, 1.0f, 0.5f, 0}};

float Skill(char param)
{
	int i = param - 'a';
	if (i < 0 || i > 8)
		return 1.0f;
	int t = game.skill_toughness;
	if (t < 0)
		t = 0;
	if (t > 4)
		t = 4;
	return toughness_rows[t][i];
}

} /* namespace om_ai */
