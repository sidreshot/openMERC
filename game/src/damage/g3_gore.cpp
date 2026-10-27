// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - gore zones: setup from the model's visible parts, wound levels and skins,
// severing, head explosions, exit wounds, gibbing, severed pieces, stuck knives, bolt-ons knocked off by hits,
// violence locks and the gore savegame data.
// Implements: SPEC-721 §1-§13, SPEC-720 "Character zone armour"
#include "g3_local.h"
#include "../effects/fx.h"
#include "../ghoulglue/gg_ghoul.h"

using namespace ghoul;

G3Locks g3locks;
int g3_gibCounter;
int g3_gibPieces;

const char *const g3_humanZoneNames[GZ_NUM_HUMAN] = {"GZ_HEAD_FRONT", "GZ_HEAD_BACK", "GZ_NECK",
	"GZ_SHLDR_RIGHT_FRONT", "GZ_SHLDR_RIGHT_BACK", "GZ_SHLDR_LEFT_FRONT", "GZ_SHLDR_LEFT_BACK", "GZ_CHEST_FRONT",
	"GZ_CHEST_BACK", "GZ_ARM_UPPER_RIGHT", "GZ_ARM_UPPER_LEFT", "GZ_ARM_LOWER_RIGHT", "GZ_ARM_LOWER_LEFT",
	"GZ_GUT_FRONT", "GZ_GUT_BACK", "GZ_GROIN", "GZ_LEG_UPPER_RIGHT_FRONT", "GZ_LEG_UPPER_RIGHT_BACK",
	"GZ_LEG_UPPER_LEFT_FRONT", "GZ_LEG_UPPER_LEFT_BACK", "GZ_LEG_LOWER_RIGHT", "GZ_LEG_LOWER_LEFT", "GZ_FOOT_RIGHT",
	"GZ_FOOT_LEFT", "GZ_GUT_FRONT_EXTRA", "GZ_GUT_BACK_EXTRA"};

// ---------------------------------------------------------------------------------------------------------------
// tables (generated from SPEC-721 by tools/gen_gore_tables.py)
// ---------------------------------------------------------------------------------------------------------------
struct G3ZoneFixed {
	const char *goreBolt, *pieceCapBolt, *bodyCapBolt, *capModel, *capModelBolt;
	int segment, next, link;
};
struct G3SeverDef {
	int segment;
	int zones[12];
	const char *const *items;
};
enum { VF_WOUND, VF_OWNER, VF_BODYCAP, VF_PIECECAP, VF_BODYCAPBOLT, VF_PIECECAPBOLT, VF_NEXT, VF_SEVER };
struct G3VarSet {
	int zone;
	int field;
	const char *value;
	int ivalue;
	const char *const *sever;
};
struct G3VarRow {
	const char *mesh;
	int chain;
	G3VarSet sets[24];
};
#include "g3_gore_tables.inc"

// ---------------------------------------------------------------------------------------------------------------
void Gore_ReadLocks(void)
{
	// the body's note/bolt callbacks go into the glue's savegame callback table, once, in a fixed order (G9)
	static bool registered;
	if (!registered) {
		registered = true;
		for (int i = 0; i < 13; i++)
			GG_RegisterCallback(G3_NoteCallback(i));
		for (int i = 0; i < 5; i++)
			GG_RegisterCallback(G3_BoltCallback(i));
	}
	g3locks.blood = G3_Cvar("lock_blood") != 0;
	g3locks.gorezones = G3_Cvar("lock_gorezones") != 0;
	g3locks.sever = G3_Cvar("lock_sever") != 0;
	g3locks.deaths = G3_Cvar("lock_deaths") != 0;
	g3locks.textures = G3_Cvar("lock_textures") != 0;
	G3_DPrintf("G3 violence locks: blood %d gorezones %d sever %d deaths %d textures %d\n", g3locks.blood,
	           g3locks.gorezones, g3locks.sever, g3locks.deaths, g3locks.textures); // OpenMerc diagnostic
}

void G3G_ResetModels(void);

void Gore_LevelStart(void)
{
	// SPEC-760 B1.3 step 3 "reset bodies": every body of the previous level is released here (bodies of players and
	// corpses were destroyed before the entities were cleared; AI-owned ones are released now, their AI objects
	// are released by om_ai::LevelStart without touching them)
	int live = Body_Count();
	Body_ReleaseAll();
	if (G3_Cvar("g3_debug") != 0) // OpenMerc diagnostic
		G3_Printf("G3: level start released %d bodies\n", live);
	G3G_ResetModels();
	G3_ResetHint();
	g3_gibCounter = 0;
	g3_gibPieces = 0;
	// the 13 gore piece models of SPEC-791 §5 are registered by the glue (G9, GG_LevelSpawn)
	fx::g_arm_scripted_kill = Body_ScriptedKill; // G7: environ_invisible_attack mouth shot (SPEC-720 11.1)
}

void Gore_ResetGibCounter(void)
{
	g3_gibCounter = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// setup (§1)
// ---------------------------------------------------------------------------------------------------------------
G3Gore::G3Gore(G3Body *b, int n)
{
	memset(this, 0, sizeof *this);
	body = b;
	numZones = n > G3_MAX_ZONES ? G3_MAX_ZONES : n;
	killZone = -1;
	lastZone = -1;
	for (int i = 0; i < G3_MAX_ZONES; i++) {
		zones[i].nextZone = zones[i].link = -1;
		zones[i].scale = 1.0f;
	}
}

static void ClearZone(G3Zone &z)
{
	float scale = z.scale;
	memset(&z, 0, sizeof z);
	z.scale = scale;
	z.nextZone = z.link = -1;
}

void G3Gore::Setup(void)
{
	IGhoulInst *inst = body->Inst();
	for (int i = 0; i < numZones; i++)
		ClearZone(zones[i]);
	memset(skins, 0, sizeof skins);
	gzMaterial = G3G_Material(inst, "gz");
	cMaterial = G3G_Material(inst, "c");
	cBloodSkin = G3G_Skin(inst, cMaterial, "c_blood");
	static const char *const names[3][4] = {{"gz_sing1", "gz_sing2", "gz_sing3", "gz_sing_k"},
		{"gz_multi1", "gz_multi2", "gz_multi3", "gz_multi_k"}, {"gz_mass1", "gz_mass2", "gz_mass3", "gz_mass_k"}};
	if (gzMaterial)
		for (int l = 0; l < 3; l++)
			for (int v = 0; v < 4; v++)
				skins[l][v] = G3G_Skin(inst, gzMaterial, names[l][v]);
	switch (body->family) {
	case BODY_MESO:
	case BODY_MESO_ARMOURED:
	case BODY_ECTO:
	case BODY_FEMALE:
		SetupHuman(body->family);
		break;
	case BODY_HUSKY:
	case BODY_ROTTWEILER:
		SetupDog(body->family);
		break;
	case BODY_COW:
		SetupCow();
		break;
	}
}

static void ResolveSever(IGhoulInst *inst, G3Zone &z, const char *const *items)
{
	z.nsever = 0;
	if (!items)
		return;
	for (int i = 0; items[i] && z.nsever < G3_MAX_SEVER; i++) {
		const char *s = items[i];
		G3SeverItem it;
		if (!strcmp(s, "+headarms")) {
			it.id = 0;
			it.kind = 2;
		} else if (!strncmp(s, "+zone:", 6)) {
			it.kind = 3;
			it.id = (GhoulID)(!strcmp(s + 6, "LEG_LOWER_RIGHT") ? GZ_LEG_LOWER_RIGHT : GZ_LEG_LOWER_LEFT);
		} else if (!strncmp(s, "b:", 2)) {
			it.kind = 1;
			it.id = G3G_Part(inst, s + 2);
			if (!it.id)
				continue;
		} else {
			it.kind = 0;
			it.id = G3G_Part(inst, s);
			if (!it.id)
				continue;
		}
		z.sever[z.nsever++] = it;
	}
}

void G3Gore::SetupHuman(int fam)
{
	IGhoulInst *inst = body->Inst();
	const G3ZoneFixed *fixed;
	const G3SeverDef *sever;
	const G3VarRow *vars;
	if (fam == BODY_ECTO) {
		fixed = g3_ectoFixed;
		sever = g3_ectoSever;
		vars = g3_ectoVariants;
	} else if (fam == BODY_FEMALE) {
		fixed = g3_femaleFixed;
		sever = g3_femaleSever;
		vars = g3_femaleVariants;
	} else {
		fixed = g3_mesoFixed;
		sever = g3_mesoSever;
		vars = g3_mesoVariants;
	}
	for (int i = 0; i < numZones && i < GZ_NUM_HUMAN; i++) {
		G3Zone &z = zones[i];
		const G3ZoneFixed &f = fixed[i];
		z.goreBolt = G3G_Part(inst, f.goreBolt);
		z.pieceCapBolt = G3G_Part(inst, f.pieceCapBolt);
		z.bodyCapBolt = G3G_Part(inst, f.bodyCapBolt);
		z.capModel = f.capModel;
		z.capModelBolt = f.capModelBolt;
		z.segment = f.segment;
		z.nextZone = f.next;
		z.link = f.link;
	}
	for (int g = 0; sever[g].items; g++)
		for (int k = 0; k < 12 && sever[g].zones[k] >= 0; k++)
			ResolveSever(inst, zones[sever[g].zones[k]], sever[g].items);
	// variant chains: the first shown mesh of each chain decides
	int decided = -1;
	for (int r = 0; vars[r].mesh; r++) {
		const G3VarRow &row = vars[r];
		if (row.chain == decided)
			continue;
		GhoulID mesh = G3G_Part(inst, row.mesh);
		if (!mesh || !inst->IsPartOn(mesh))
			continue;
		decided = row.chain;
		for (int s = 0; row.sets[s].zone >= 0; s++) {
			const G3VarSet &vs = row.sets[s];
			G3Zone &z = zones[vs.zone];
			switch (vs.field) {
			case VF_WOUND: z.woundMesh = G3G_Part(inst, vs.value); break;
			case VF_OWNER: z.owner = G3G_Part(inst, vs.value); break;
			case VF_BODYCAP: z.bodyCap = G3G_Part(inst, vs.value); break;
			case VF_PIECECAP: z.pieceCap = G3G_Part(inst, vs.value); break;
			case VF_BODYCAPBOLT: z.bodyCapBolt = G3G_Part(inst, vs.value); z.headCapNone = !z.bodyCapBolt; break;
			case VF_PIECECAPBOLT: z.pieceCapBolt = G3G_Part(inst, vs.value); break;
			case VF_NEXT: z.nextZone = vs.ivalue; break;
			case VF_SEVER: ResolveSever(inst, z, vs.sever); break;
			}
		}
	}
	// head cap bolt-on: one draw per setup
	float r = G3_flrand(0, 1);
	const char *head = r >= 0.7f ? "g_half_head" : "g_neck_torn_anim2";
	zones[GZ_HEAD_FRONT].capModel = zones[GZ_HEAD_BACK].capModel = head;
	gutsOut = false;
	// rule 1.5 and the zones' cap bolt-on records, registered at the set-up (observed black box: the original
	// registers a `g_half_head` record per character head at level start, before the first `g_guts_lrg`)
	RegisterHalfHead(NULL);
	RegisterCapModels();
}

// Rule 1.5: `g_half_head` is registered with the character's head skin (material `h`), skin `gz_half_head` on its
// `gz` material and the dead face (material `f`: the face prefix with `_d`), in that order; characters with the same
// head and face share the record (observed: `H=H_B_6_S GZ=GZ_HALF_HEAD F=F_B_6_D`). Nothing while the instance shows
// no head or face skin yet (deathmatch body precache temporaries); the face set-up calls it again.
void G3Gore::RegisterHalfHead(const char *facePrefix)
{
	IGhoulInst *inst = body->Inst();
	IGhoulObject *o = G3G_Object(inst);
	if (!o || halfHeadObj)
		return;
	GhoulID hm = o->FindMaterial("h"), fm = o->FindMaterial("f");
	if (!hm || !fm)
		return;
	char head[48] = "", face[48] = "";
	GhoulID hs = inst->GetSkin(hm, 0, 0);
	if (hs)
		o->GetSkinName(hs, head);
	size_t pl = facePrefix ? strlen(facePrefix) : 0;
	if (pl) // the body's face prefix ends with '_' ("f_john_" + expression letter)
		Com_sprintf(face, sizeof face, facePrefix[pl - 1] == '_' ? "%sd" : "%s_d", facePrefix);
	else {
		GhoulID fs = inst->GetSkin(fm, 0, 0);
		if (fs) {
			o->GetSkinName(fs, face);
			char *u = strrchr(face, '_');
			if (u && u[1] && !u[2])
				strcpy(u, "_d");
		}
	}
	if (!head[0] || !face[0])
		return;
	char key[100];
	Com_sprintf(key, sizeof key, "%s/%s", head, face);
	GGModel *m = GG_FindModel("Enemy/bolt", "g_half_head", false, key);
	if (!m)
		return;
	if (!GG_Prepared(m)) {
		GG_RegisterSkin(m, "h", head);
		GG_RegisterSkin(m, "gz", "gz_half_head");
		GG_RegisterSkin(m, "f", face);
		GG_Prepare(m);
	}
	halfHeadObj = GG_Object(m);
	strncpy(halfHeadH, head, sizeof halfHeadH - 1);
	strncpy(halfHeadF, face, sizeof halfHeadF - 1);
}

// the zones' cap bolt-on models as records with all their skins (`g_guts_lrg` of the torso zones is the one not
// already registered by the glue's level start list; observed `G_GUTS_LRG=G_GUTS_LRG` on every SP and DM map)
void G3Gore::RegisterCapModels(void)
{
	for (int i = 0; i < numZones; i++) {
		const char *cm = zones[i].capModel;
		if (cm && Q_stricmp(cm, "g_half_head"))
			GG_FindModel("Enemy/bolt", cm, true);
	}
}

// Dog tables (SPEC-721 "Dog zones"): husky / rottweiler
struct DogZone {
	const char *wound, *ownerH, *ownerR, *gore, *bodyCapH, *bodyCapR, *pieceCapH, *pieceCapR, *capBoltH, *capBoltR;
	const char *severH[3], *severR[3];
	int next;
};
static const DogZone dogZones[10] = {
	{"GZ_HEAD", "_husky_torso", "_rot_torso", "gbolt_head", NULL, NULL, NULL, NULL, NULL, NULL, {NULL}, {NULL}, -1},
	{"GZ_LCHEST", "_husky_torso", "_rot_torso", "gbolt_lchest", NULL, NULL, NULL, NULL, NULL, NULL, {NULL}, {NULL}, -1},
	{"GZ_RCHEST", "_husky_torso", "_rot_torso", "gbolt_rchest", NULL, NULL, NULL, NULL, NULL, NULL, {NULL}, {NULL}, -1},
	{"GZ_FRONT", "_husky_torso", "_rot_torso", "gbolt_front", NULL, NULL, NULL, NULL, NULL, NULL, {NULL}, {NULL}, -1},
	{NULL, "_husky_lleg_front", "_rot_lleg_front", NULL, "_CAP_HUSKY_TORSO_LSHOLDR", "_CAP_ROT_TORSO_LSHOULDER",
		"_CAP_HUSKY__LLEG_LSHOLDR", "_CAP_ROT_LLEG_SHOULDER", "GBOLT_CAP_HUSKY_TORSO_LSHOLDR",
		"GBOLT_CAP_ROT_TORSO_LSHOULDER", {"_HUSKY_LLEG_FRONT", NULL}, {"_ROT_LLEG_FRONT", NULL}, -1},
	{NULL, "_husky_rleg_front", "_rot_rleg_front", NULL, "_CAP_HUSKY_TORSO_RSHOLDR", "_CAP_ROT_TORSO_RSHOULDER",
		"_CAP_HUSKY_RLEG_RSHOLDR", "_CAP_ROT_RLEG_SHOULDER", "GBOLT_CAP_HUSKY_TORSO_RSHOLDR",
		"GBOLT_CAP_ROT_TORSO_RSHOULDER", {"_HUSKY_RLEG_FRONT", NULL}, {"_ROT_RLEG_FRONT", NULL}, -1},
	{"GZ_LHIP", "_husky_lhip", "_rot_torso", NULL, NULL, NULL, NULL, NULL, NULL, NULL, {NULL}, {NULL}, -1},
	{"GZ_RHIP", "_husky_rhip", "_rot_rleg_back", NULL, "_CAP_HUSKY_TORSO_RHIP", "_CAP_ROT_TORSO_RHIP",
		"_CAP_HUSKY_RHIP_BODY", "_CAP_ROT_RLEG_HIP", "GBOLT_CAP_HUSKY_TORSO_RHIP", "GBOLT_CAP_ROT_TORSO_RHIP",
		{"_HUSKY_RHIP", "_HUSKY_RLEG_BACK", NULL}, {"_ROT_RLEG_BACK", NULL}, 9},
	{NULL, "_husky_lleg_back", "_rot_lleg_back", NULL, "_CAP_HUSKY_LHIP_LLEG", "_CAP_ROT_TORSO_LHIP",
		"_CAP_HUSKY_LLEG_LHIP", "_CAP_ROT_LLEG_HIP", "GBOLT_CAP_HUSKY_LHIP_LLEG", "GBOLT_CAP_ROT_TORSO_LHIP",
		{"_HUSKY_LLEG_BACK", NULL}, {"_ROT_LLEG_BACK", NULL}, -1},
	{NULL, "_husky_rleg_back", "_rot_rleg_back", NULL, "_CAP_HUSKY_TORSO_RHIP", "_CAP_ROT_TORSO_RHIP",
		"_CAP_HUSKY_RHIP_BODY", "_CAP_ROT_RLEG_HIP", "GBOLT_CAP_HUSKY_TORSO_RHIP", "GBOLT_CAP_ROT_TORSO_RHIP",
		{"_HUSKY_RHIP", "_HUSKY_RLEG_BACK", NULL}, {"_ROT_RLEG_BACK", NULL}, 7},
};

void G3Gore::SetupDog(int fam)
{
	IGhoulInst *inst = body->Inst();
	bool h = fam == BODY_HUSKY;
	for (int i = 0; i < numZones && i < 10; i++) {
		const DogZone &d = dogZones[i];
		G3Zone &z = zones[i];
		z.woundMesh = G3G_Part(inst, d.wound);
		GhoulID own = G3G_Part(inst, h ? d.ownerH : d.ownerR);
		z.owner = (own && inst->IsPartOn(own)) ? own : 0;
		z.goreBolt = G3G_Part(inst, d.gore);
		z.bodyCap = G3G_Part(inst, h ? d.bodyCapH : d.bodyCapR);
		z.pieceCap = G3G_Part(inst, h ? d.pieceCapH : d.pieceCapR);
		z.bodyCapBolt = G3G_Part(inst, h ? d.capBoltH : d.capBoltR);
		z.capModel = NULL; // default g_cap1
		z.segment = z.bodyCap ? SEG_ANIMAL : SEG_END;
		z.nextZone = d.next;
		z.link = -1;
		const char *const *sv = h ? d.severH : d.severR;
		z.nsever = 0;
		for (int k = 0; k < 3 && sv[k]; k++) {
			GhoulID p = G3G_Part(inst, sv[k]);
			if (p) {
				z.sever[z.nsever].id = p;
				z.sever[z.nsever].kind = 0;
				z.nsever++;
			}
		}
	}
}

void G3Gore::SetupCow(void)
{
	IGhoulInst *inst = body->Inst();
	static const char *const w[6] = {"GZ_HEAD", "GZ_CHEST", "GZ_FRONT_R", "GZ_FRONT_L", "GZ_BACK_R", "GZ_BACK_L"};
	static const char *const b[6] = {"gbolt_head", "gbolt_chest", "gbolt_front_r", "gbolt_front_l", "gbolt_back_r",
		"gbolt_back_l"};
	GhoulID own = G3G_Part(inst, "_body_1");
	for (int i = 0; i < numZones && i < 6; i++) {
		G3Zone &z = zones[i];
		z.woundMesh = G3G_Part(inst, w[i]);
		z.goreBolt = G3G_Part(inst, b[i]);
		z.owner = (own && inst->IsPartOn(own)) ? own : 0;
		z.segment = SEG_END;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------------------------
bool G3Gore::OwnerShown(int zi)
{
	IGhoulInst *inst = body->Inst();
	const G3Zone &z = zones[zi];
	return z.owner && inst && inst->IsPartOn(z.owner);
}

static bool IsHuman(const G3Body *b)
{
	return G3_IsHumanFamily(b->family);
}

void G3Gore::ApplyWoundSkin(int zi, int level, int variant)
{
	IGhoulInst *inst = body->Inst();
	G3Zone &z = zones[zi];
	if (!inst || !z.woundMesh || level < 1 || level > 3)
		return;
	if (variant < 0 || variant > 3)
		variant = 0;
	GhoulID s = skins[level - 1][variant];
	if (s && gzMaterial)
		inst->SetSkin(gzMaterial, s, z.woundMesh);
}

// index (0..3) of the skin the zone shows among its old level's skins; 4 = none of them
int G3Gore::SkinVariant(int zi)
{
	IGhoulInst *inst = body->Inst();
	G3Zone &z = zones[zi];
	if (!inst || !z.woundMesh || z.level < 1 || !gzMaterial)
		return 4;
	GhoulID cur = inst->GetSkin(gzMaterial, z.woundMesh, 0);
	for (int v = 0; v < 4; v++)
		if (cur && skins[z.level - 1][v] == cur)
			return v;
	return 4;
}

// SPEC-720 §10.1 as corrected by SPEC-AMEND-005 G3.5: candidates (owner part = the record's part) in zone-index
// order; a zone without gore bolt is taken only while nothing is taken and never replaces a choice; a zone with a
// gore bolt is taken when nothing is taken, when the choice has no gore bolt, or when its squared bolt distance
// (instance method 50 at the current time; identity = origin when the bolt is not in the sequence) is smaller than the
// best so far (ties keep the earlier zone). Thrown knife in deathmatch: the distance is weighted x1.3 while the
// current choice is a head zone (0..2), and the weighted value is stored.
int G3Gore::ZoneByOwnerPart(GhoulID part, const float *point, bool thrownKnifeDM)
{
	IGhoulInst *inst = body->Inst();
	int best = -1;
	float bestDist = 0;
	bool bestHasBolt = false;
	if (!part)
		return -1;
	for (int i = 0; i < numZones; i++) {
		const G3Zone &z = zones[i];
		if (z.owner != part)
			continue;
		if (!z.goreBolt) {
			if (best < 0)
				best = i;
			continue;
		}
		Matrix4 m;
		G3G_BoltMatrix(inst, z.goreBolt, &m, 5, true);
		vec3_t bp = {m.m[3][0], m.m[3][1], m.m[3][2]}, dd;
		VectorSubtract(point, bp, dd);
		float d = DotProduct(dd, dd);
		if (thrownKnifeDM && best >= 0 && best <= GZ_NECK)
			d *= 1.3f;
		if (best < 0 || !bestHasBolt || d < bestDist) {
			best = i;
			bestDist = d;
			bestHasBolt = true;
		}
	}
	return best;
}

// ---------------------------------------------------------------------------------------------------------------
// wounding (§2, §12)
// ---------------------------------------------------------------------------------------------------------------
bool G3Gore::WoundZone(int zi, int d, int H, int mod, int dflags, edict_t *attacker, const vec3_t dir)
{
	(void)attacker;
	(void)dir;
	if (zi < 0 || zi >= numZones)
		return false;
	G3Zone &z = zones[zi];
	IGhoulInst *inst = body->Inst();
	if (!inst || !z.woundMesh || !OwnerShown(zi) || d < 1)
		return false;
	bool human = IsHuman(body);
	// 2.1 accumulated damage
	if (!human)
		z.damage += d; // §12: animals add the full damage
	else if (H >= 1)
		z.damage += d < H ? d : H;
	else
		z.damage += d > 4 ? d : 10;
	int L = LevelOf(z.damage);
	if (!g3locks.gorezones)
		inst->SetPartOnOff(z.woundMesh, true);
	if (L > z.level) {
		// bolt-ons on the zone's sever-group bolts: only "falls off on any hit" accessories drop
		G3Hit fake;
		memset(&fake, 0, sizeof fake);
		fake.targ = body->ent;
		fake.damage = 1;
		for (int k = 0; k < z.nsever; k++)
			if (z.sever[k].kind == 1) {
				for (int n = 0;;) {
					IGhoulInst *c = G3G_ChildOnBolt(inst, z.sever[k].id, n);
					if (!c)
						break;
					int before = G3G_NumChildren(inst);
					KnockOff(c, 1, fake, true);
					if (G3G_NumChildren(inst) == before)
						n++;
				}
			}
		int v;
		if (!human)
			v = G3_irand(0, 0x7fff) % 3;
		else {
			v = G3_irand(0, 2);
			if ((mod & ~DMG_MOD_FRIENDLY) == MOD_KNIFE_SLASH)
				v = 3;
			if (z.level > 0 && !g3locks.gorezones && v != 3) {
				v = SkinVariant(zi);
				if (v >= 3)
					v = G3_irand(0, 2);
			}
		}
		if (z.goreBolt && H >= 1 && !(dflags & DMGF_TORSO_BLAST))
			G3FX_StumpBlood(body->ent, z.goreBolt);
		if (human && zi == GZ_GUT_FRONT && d >= H && (dflags & DMGF_SPREAD) && !g3locks.gorezones && !gutsOut) {
			GhoulID gb = G3G_Part(inst, "gbolt_gut_f");
			if (gb) {
				G3G_RemoveChildrenOnBolt(inst, gb);
				IGhoulObject *o = G3G_ModelObject("Enemy/bolt", "guts_bolton");
				IGhoulInst *g = G3G_AttachNew(inst, gb, o, "to_gbolt_gut_f", 1.0f, "g_exit_guts_anim1", false);
				if (g)
					body->gutsInst = g; // anim2 loops after anim1 ends (G3Body::Think)
				gutsOut = true;
			}
		}
		if (!g3locks.gorezones) {
			ApplyWoundSkin(zi, L, v);
			if (z.link >= 0 && z.link < numZones) {
				G3Zone &lk = zones[z.link];
				lk.damage = z.damage;
				lk.level = L;
				if (lk.woundMesh) {
					inst->SetPartOnOff(lk.woundMesh, true);
					ApplyWoundSkin(z.link, L, v);
				}
			}
			z.level = L;
		}
	}
	if (d < H)
		body->GeneralVoice("Hit", 3);
	lastZone = zi;
	return d >= H;
}

// Rule 2.7 as corrected by SPEC-AMEND-005 G3.1, called by the bodies for each hit zone after that zone's wounding,
// severing or head explosion: a killing hit (d > H; on a corpse every hit with d >= 1) on a zone with a wound mesh
// puts `gore/bloodcloud_s` (50) on the zone's gore bolt, else on its body-side cap bolt (the owner part need not be
// shown).
void G3Gore::KillingCloud(int zi, int d, int H)
{
	if (zi < 0 || zi >= numZones || d < 1 || d <= H)
		return;
	const G3Zone &z = zones[zi];
	if (!z.woundMesh)
		return;
	GhoulID cb = z.goreBolt ? z.goreBolt : z.bodyCapBolt;
	if (cb)
		G3FX_BloodCloud(body->ent, cb, 50);
}

// Blood-pool tracking (rule 2.7 first sentence, §7; G3.1): once per body, the first hit zone of the first hit with
// d > H; no wound-mesh condition. The zone is also the "killing zone" of SPEC-722 death rule 11.
void G3Gore::NoteKillingHit(int firstZone, int d, int H)
{
	if (killZone >= 0 || firstZone < 0 || firstZone >= numZones || d <= H)
		return;
	killZone = firstZone;
	TrackKillBolt();
}

void G3Gore::TrackKillBolt(void)
{
	if (killZone < 0)
		return;
	// body-side cap bolt first, then the gore bolt; neither: under the character origin (G3.1)
	const G3Zone &z = zones[killZone];
	GhoulID b = z.bodyCapBolt ? z.bodyCapBolt : z.goreBolt;
	if (!b || !G3G_BoltWorld(body->ent, body->Inst(), b, killBoltPos))
		VectorCopy(body->ent->s.origin, killBoltPos);
}

void G3Gore::BloodPool(void)
{
	vec3_t p, end;
	if (killZone >= 0) {
		TrackKillBolt();
		VectorCopy(killBoltPos, p);
	} else
		VectorCopy(body->ent->s.origin, p);
	VectorCopy(p, end);
	end[2] -= 256;
	trace_t tr;
	memset(&tr, 0, sizeof tr);
	gix.trace(p, vec3_origin, vec3_origin, end, body->ent, MASK_SOLID, &tr);
	G3FX_BloodPool(tr.endpos);
}

// ---------------------------------------------------------------------------------------------------------------
// exit wounds (§5)
// ---------------------------------------------------------------------------------------------------------------
void G3Gore::ExitWound(int entry, int ex, int d, int H, int dflags, const vec3_t hitPos, const vec3_t dir)
{
	if (g3locks.gorezones || ex < 0 || ex >= numZones)
		return;
	IGhoulInst *inst = body->Inst();
	G3Zone &z = zones[ex];
	if (H > 0 && H < d && entry >= 0 && ex != entry && z.woundMesh && OwnerShown(ex)) {
		bool wasUnwounded = z.level == 0;
		int oldLevel = z.level;
		z.damage = 201;
		if (oldLevel < 3) {
			int v = wasUnwounded ? G3_irand(0, 2) : SkinVariant(ex);
			if (v >= 3)
				v = G3_irand(0, 2);
			z.level = 3;
			ApplyWoundSkin(ex, 3, v);
			if (z.link >= 0) {
				G3Zone &lk = zones[z.link];
				lk.damage = 201;
				lk.level = 3;
				if (lk.woundMesh) {
					inst->SetPartOnOff(lk.woundMesh, true);
					ApplyWoundSkin(z.link, 3, v);
				}
			}
			if (z.goreBolt) {
				G3G_RemoveChildrenOnBolt(inst, z.goreBolt);
				GhoulID helmet = G3G_Part(inst, "_headhelmet");
				GhoulID beard = G3G_Part(inst, "_headbeard");
				G3Zone &front = zones[GZ_HEAD_FRONT];
				bool brain = IsHuman(body) && entry == GZ_HEAD_FRONT && ex == GZ_HEAD_BACK && d > 80 && helmet &&
				             !(beard && front.owner == beard);
				if (brain) {
					for (int k = 0; k < front.nsever; k++)
						if (front.sever[k].kind == 0)
							inst->SetPartOnOff(front.sever[k].id, false);
					GhoulID top = G3G_Part(inst, "abolt_head_t");
					G3G_RemoveChildrenOnBolt(inst, top);
					inst->SetPartOnOff(helmet, true);
					front.owner = helmet;
					z.owner = helmet;
					inst->SetPartOnOff(z.woundMesh, false);
					GhoulID fw = G3G_Part(inst, "gz_headhelmet_f");
					if (fw) {
						GhoulID curSkin = front.woundMesh ? inst->GetSkin(gzMaterial, front.woundMesh, 0) : 0;
						front.woundMesh = fw;
						inst->SetPartOnOff(fw, true);
						if (curSkin)
							inst->SetSkin(gzMaterial, curSkin, fw);
					}
					z.goreBolt = top;
					IGhoulObject *o = G3G_ModelObject("Enemy/bolt", "g_brain");
					G3G_AttachNew(inst, top, o, "to_abolt_head_t", 1.17f);
				} else {
					IGhoulObject *o = G3G_ModelObject("Enemy/bolt", "g_exit_chunky_anim1");
					G3G_AttachNew(inst, z.goreBolt, o, "to_gbolt", 1.0f);
					G3FX_BloodCloud(body->ent, z.goreBolt, 100);
					if (dflags & DMGF_PROJECTILE)
						G3FX_ExitSpray(hitPos, dir);
				}
			}
		}
		if (z.woundMesh)
			inst->SetPartOnOff(z.woundMesh, true);
		return;
	}
	if (H <= 0) {
		GhoulID b = z.goreBolt ? z.goreBolt : z.bodyCapBolt;
		if (G3_irand(0, 1) == 0 && b)
			G3FX_BloodCloud(body->ent, b, 50);
		if (G3_irand(0, 2) == 0 && (dflags & DMGF_PROJECTILE))
			G3FX_ExitSpray(hitPos, dir);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// severing (§3, §4, §6, §12)
// ---------------------------------------------------------------------------------------------------------------
bool G3Gore::CanSever(int zi, int d, int H, int dflags)
{
	if (zi < 0 || zi >= numZones || g3locks.sever)
		return false;
	const G3Zone &z = zones[zi];
	if (d < H || !z.bodyCap || z.segment == SEG_TORSO || z.segment == SEG_END)
		return false;
	if (IsHuman(body))
		return G3_GoreTest() || (dflags & (DMGF_EXPLOSIVE | DMGF_SPREAD)) || z.damage > 500;
	if (body->family == BODY_COW)
		return false;
	return d > H && (G3_GoreTest() || (dflags & DMGF_SPREAD));
}

static void PieceTouch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);
static void GibPieceTouch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);
static void PieceThink(edict_t *self);
static void PieceDie(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);

static void ShowOnPiece(IGhoulInst *from, IGhoulInst *to, GhoulID part)
{
	if (!part || !to)
		return;
	if (from->IsPartOn(part)) {
		from->SetPartOnOff(part, false);
		to->SetPartOnOff(part, true);
	}
}

static void MoveBoltOns(G3Gore *g, IGhoulInst *from, IGhoulInst *to, GhoulID bolt, edict_t *piece)
{
	if (!bolt)
		return;
	IGhoulInst *last = NULL;
	for (;;) {
		IGhoulInst *c = G3G_ChildOnBolt(from, bolt);
		if (!c || c == last)
			break; // (a child the glue could not remove stays: stop)
		last = c;
		IGhoulObject *o = c->GetGhoulObject();
		const char *name = G3G_ModelName(o);
		if (!Q_stricmp(name, "w_knife") && g->body->IsStuckKnife(c)) {
			g->ReleaseKnives(c);
			continue;
		}
		if (!Q_strncasecmp(name, "w_", 2) && g->body->HandBolt(bolt) >= 0) {
			// a weapon in a hand is dropped first (SPEC-722)
			g->body->DropHandWeapon(g->body->HandBolt(bolt));
			continue;
		}
		if (!to) {
			G3G_RemoveChild(from, c);
			continue;
		}
		G3G_MoveChild(from, c, to, bolt, piece);
	}
}

// hand one zone over to the piece (rule 3.4)
// The zone hand-over of SPEC-721 rule 3.4 as refined by SPEC-AMEND-006 G3.16 (every severing: rule 3, gib limbs with
// or without a piece, Dekker split, dog/cow limbs). 1. wound mesh: to the piece (a fresh wound skin when it was not
// shown but its owner was), hidden on the body.
static void HandOverZone(G3Gore *g, int zi, IGhoulInst *inst, IGhoulInst *pinst, edict_t *piece, bool woundPiece)
{
	G3Zone &z = g->zones[zi];
	if (z.woundMesh) {
		bool shown = inst->IsPartOn(z.woundMesh);
		if (pinst) {
			if (shown)
				pinst->SetPartOnOff(z.woundMesh, true);
			else if (woundPiece && g->OwnerShown(zi) && !g3locks.gorezones) {
				pinst->SetPartOnOff(z.woundMesh, true);
				GhoulID s = g->skins[G3_irand(1, 3) - 1][G3_irand(0, 2)];
				if (s && g->gzMaterial)
					pinst->SetSkin(g->gzMaterial, s, z.woundMesh);
			}
		}
		inst->SetPartOnOff(z.woundMesh, false);
	}
	// 2. body cap mesh: shown on the piece if it was shown on the body (with a piece), hidden on the body
	if (z.bodyCap) {
		if (pinst && inst->IsPartOn(z.bodyCap))
			pinst->SetPartOnOff(z.bodyCap, true);
		inst->SetPartOnOff(z.bodyCap, false);
	}
	// 3. body-side cap bolt: its bolt-ons go to the piece (or are removed), then the bolt part is switched off
	MoveBoltOns(g, inst, pinst, z.bodyCapBolt, piece);
	if (z.bodyCapBolt)
		inst->SetPartOnOff(z.bodyCapBolt, false);
	// 4. piece-side cap bolt: a stuck knife becomes a pickup, every other bolt-on is removed; then the part is off
	if (z.pieceCapBolt) {
		for (int guard = 0; guard < 16; guard++) {
			IGhoulInst *c = G3G_ChildOnBolt(inst, z.pieceCapBolt);
			if (!c)
				break;
			if (g->body->IsStuckKnife(c))
				g->ReleaseKnives(c);
			else
				G3G_RemoveChild(inst, c);
		}
		inst->SetPartOnOff(z.pieceCapBolt, false);
	}
	// 5. gore bolt: its bolt-ons go to the piece (or are removed); its part stays on
	MoveBoltOns(g, inst, pinst, z.goreBolt, piece);
}

static void WalkSever(G3Gore *g, int zi, IGhoulInst *inst, IGhoulInst *pinst, edict_t *piece, int depth);

static void HandOverChain(G3Gore *g, int start, IGhoulInst *inst, IGhoulInst *pinst, edict_t *piece, int depth)
{
	int zi = start;
	for (int guard = 0; zi >= 0 && guard < G3_MAX_ZONES; guard++) {
		HandOverZone(g, zi, inst, pinst, piece, true);
		zi = g->zones[zi].nextZone;
		if (zi == start)
			break;
	}
	if (depth > 0)
		WalkSever(g, start, inst, pinst, piece, depth - 1);
}

static void WalkSever(G3Gore *g, int zi, IGhoulInst *inst, IGhoulInst *pinst, edict_t *piece, int depth)
{
	G3Zone &z = g->zones[zi];
	for (int k = 0; k < z.nsever; k++) {
		const G3SeverItem &it = z.sever[k];
		switch (it.kind) {
		case 0:
			ShowOnPiece(inst, pinst, it.id);
			break;
		case 1:
			MoveBoltOns(g, inst, pinst, it.id, piece);
			break;
		case 2: // head and both upper arms
			if (depth > 0) {
				HandOverChain(g, GZ_HEAD_FRONT, inst, pinst, piece, depth);
				HandOverChain(g, GZ_ARM_UPPER_RIGHT, inst, pinst, piece, depth);
				HandOverChain(g, GZ_ARM_UPPER_LEFT, inst, pinst, piece, depth);
			}
			break;
		case 3:
			if (depth > 0)
				HandOverChain(g, it.id, inst, pinst, piece, depth);
			break;
		}
	}
}

static void AttachCap(G3Gore *g, IGhoulInst *on, GhoulID bolt, const G3Zone &z)
{
	if (!on || !bolt)
		return;
	const char *model = z.capModel ? z.capModel : "g_cap1";
	const char *mbolt = z.capModel ? z.capModelBolt : "to_gbolt";
	bool half = !Q_stricmp(model, "g_half_head");
	if (half && !g->halfHeadObj)
		g->RegisterHalfHead(NULL);
	IGhoulObject *o = half && g->halfHeadObj ? g->halfHeadObj : G3G_ModelObject("Enemy/bolt", model);
	IGhoulInst *c = G3G_AttachNew(on, bolt, o, mbolt, 1.0f);
	if (c && half && g->halfHeadObj) {
		// the character's own record: its registered head, half-head and dead-face skins
		IGhoulObject *co = c->GetGhoulObject();
		const char *mats[3] = {"h", "gz", "f"}, *names[3] = {g->halfHeadH, "gz_half_head", g->halfHeadF};
		for (int k = 0; k < 3; k++) {
			GhoulID cm = co->FindMaterial(mats[k]), cs = cm ? co->FindSkin(cm, names[k], 0) : 0;
			if (cs)
				c->SetSkin(cm, cs, 0);
		}
	} else if (c && half) {
		// wear the character's head and face skins (SPEC-721 1.5)
		IGhoulInst *inst = g->body->Inst();
		GhoulID hm = G3G_Material(inst, "h"), fm = G3G_Material(inst, "f");
		IGhoulObject *co = c->GetGhoulObject();
		char name[128];
		if (hm) {
			GhoulID s = inst->GetSkin(hm, 0, 0);
			if (s) {
				inst->GetGhoulObject()->GetSkinName(s, name);
				GhoulID cm = co->FindMaterial("h"), cs = cm ? co->FindSkin(cm, name, 0) : 0;
				if (cs)
					c->SetSkin(cm, cs, 0);
			}
		}
		if (fm) {
			GhoulID s = inst->GetSkin(fm, 0, 0);
			if (s) {
				inst->GetGhoulObject()->GetSkinName(s, name);
				GhoulID cm = co->FindMaterial("f"), cs = cm ? co->FindSkin(cm, name, 0) : 0;
				if (cs)
					c->SetSkin(cm, cs, 0);
			}
		}
		GhoulID gm = co->FindMaterial("gz"), gs = gm ? co->FindSkin(gm, "gz_half_head", 0) : 0;
		if (gs)
			c->SetSkin(gm, gs, 0);
	}
}

// Severed piece (SPEC-721 §6 with SPEC-AMEND-005 G3.11): a copy of the character's instance that takes over its
// aligned bolt and is paused now (it keeps the pose of the cut); all its meshes hidden (rule 3 shows the handed-over
// ones). Origin = world position of the zone's piece-side cap bolt (else the body-side cap bolt); the instance
// transform = the character's instance transform followed by the character's entity frame, expressed in the piece's
// frame (that origin, zero angles); then yaw flrand(-30, 30) turns the piece about the cap bolt. Fallback (no cap bolt
// or no owner part): the piece plays `std_istand_n_a_n` at origin + 24 up (no piece when it cannot play it).
// Physics (SPEC-760 names): ground friction 0.5, gravity 1.0, air friction 0.1, buoyancy 0.7, elasticity 0.5.
static edict_t *SpawnPiece(G3Gore *g, int zi, bool gibPiece, const vec3_t gibDir, IGhoulInst **pinstOut)
{
	edict_t *ch = g->body->ent;
	IGhoulInst *inst = g->body->Inst();
	*pinstOut = NULL;
	if (!inst)
		return NULL;
	const G3Zone &z = g->zones[zi];
	GhoulID capBolt = z.pieceCapBolt ? z.pieceCapBolt : z.bodyCapBolt;
	bool fallback = !capBolt || !z.owner;
	GhoulID stand = 0;
	if (fallback) {
		stand = G3G_Sequence(inst, G3G_ModelDirOfInst(inst), "std_istand_n_a_n");
		if (!stand)
			return NULL; // the body cannot play it: no piece
	}
	edict_t *p = G_Spawn();
	if (!p)
		return NULL;
	IGhoulInst *pi = G3G_CloneFor(inst, p);
	if (!pi) {
		G_FreeEdict(p);
		return NULL;
	}
	pi->SetAttachBolt(inst->GetAttachBolt());
	pi->SetAllPartsOnOff(false);
	p->s.renderfx = RF_GHOUL;
	p->classname = (char *)"g3_piece";
	p->g3flags |= G3F_PIECE;
	p->owner = ch;
	p->solid = SOLID_NOT;
	p->clipmask = 0x4010003;
	p->movetype = MOVETYPE_DANTOUCH;
	p->health = 300;
	p->takedamage = DAMAGE_YES;
	p->die = PieceDie;
	p->friction = 0.5f;
	p->gravity = 1.0f;
	p->airfriction = 0.1f;
	p->buoyancy = 0.7f;
	p->elasticity = 0.5f;
	VectorClear(p->s.angles);
	Matrix4 bm;
	if (!fallback && G3G_BoltMatrix(inst, capBolt, &bm, 5, true)) {
		vec3_t local = {bm.m[3][0], bm.m[3][1], bm.m[3][2]};
		G3_EntityToWorld(ch, local, p->s.origin);
		// T_piece = T_char · E_char · E_piece⁻¹ (row vectors): rotation rows × the character's axes, translation
		// (t_char · axes + character origin − piece origin)
		Matrix4 t, n;
		inst->GetTransform(&t);
		vec3_t ax[3];
		G3_EntityAxes(ch, ax[0], ax[1], ax[2]);
		memset(&n, 0, sizeof n);
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 3; c++) {
				float v = 0;
				for (int k = 0; k < 3; k++)
					v += t.m[r][k] * ax[k][c];
				n.m[r][c] = v;
			}
		for (int c = 0; c < 3; c++)
			n.m[3][c] += ch->s.origin[c] - p->s.origin[c];
		n.m[3][3] = 1;
		n.flags = 0;
		pi->SetTransform(&n);
		pi->Pause(G3_Time());
	} else {
		VectorCopy(ch->s.origin, p->s.origin);
		p->s.origin[2] += 24;
		pi->Play(stand, G3_Time(), 0, true, ghoul::kPlayLoop, false, false);
		fallback = true;
	}
	p->s.angles[YAW] = G3_flrand(-30, 30);
	*pinstOut = pi;
	// motion
	if (fallback) {
		p->velocity[0] = 75 * gibDir[0];
		p->velocity[1] = 75 * gibDir[1];
		p->velocity[2] = (float)G3_irand(30, 70);
		VectorSet(p->mins, -8, -8, -8);
		VectorSet(p->maxs, 8, 8, 0);
		p->count = 2; // box set (FinishPiece keeps it)
	} else if (gibPiece) {
		float sp = G3_flrand(100, 200);
		VectorScale(gibDir, sp, p->velocity);
	} else {
		vec3_t u;
		VectorSubtract(p->s.origin, ch->s.origin, u);
		VectorNormalize(u);
		float sp = G3_flrand(100, 200);
		for (int i = 0; i < 3; i++)
			p->velocity[i] = G3_flrand(-50, 50) + u[i] * sp;
	}
	p->touch = gibPiece ? GibPieceTouch : PieceTouch;
	p->nextthink = level.time + 0.1f;
	p->think = PieceThink;
	// lifetime: the type-13 limb AI (G4, SPEC-732 §9) decides; without it G3 removes the piece after 30 s
	p->dietime = g3hooks.limbAI ? 0 : level.time + 30.0f;
	return p;
}

static void FinishPiece(G3Gore *g, edict_t *p, IGhoulInst *pi, int zi, bool gibPiece)
{
	if (!p || !pi)
		return;
	const G3Zone &z = g->zones[zi];
	if (p->count != 2) {
		// box: the exact posed bounds of the zone's owner part, 0.3 s ahead, along the character's entity axes;
		// only the extent is used, centred on the origin (G3.11)
		Matrix4 root;
		g->body->Inst()->GetTransform(&root);
		vec3_t mn, mx;
		pi->GetBounds(G3_Time() + 0.3f, &root, mn, mx, z.owner, true);
		for (int i = 0; i < 3; i++) {
			float h = (mx[i] - mn[i]) * 0.5f;
			if (h < 0)
				h = 0;
			p->mins[i] = -h;
			p->maxs[i] = h;
		}
	}
	p->count = 0;
	if (p->maxs[2] - p->mins[2] > p->maxs[0] - p->mins[0] && p->maxs[2] - p->mins[2] > p->maxs[1] - p->mins[1])
		p->avelocity[PITCH] = -150;
	// stump blood on the piece's cap bolt only for rule-3 pieces; gib pieces get none (G3.11; observed black box)
	if (z.pieceCapBolt && !gibPiece)
		G3FX_StumpBlood(p, z.pieceCapBolt);
	gix.linkentity(p);
	if (g3hooks.limbAI)
		g3hooks.limbAI(p);
}

edict_t *G3Gore::Sever(int zi, int exitz, bool gibPiece, const vec3_t gibDir, bool keep)
{
	IGhoulInst *inst = body->Inst();
	if (!inst || zi < 0 || zi >= numZones)
		return NULL;
	G3Zone &z = zones[zi];
	bool head = z.segment == SEG_HEAD;
	edict_t *piece = NULL;
	IGhoulInst *pinst = NULL;
	if (!head && (!gibPiece || (g3_gibCounter < 5 && g3_gibPieces < 2))) {
		piece = SpawnPiece(this, zi, gibPiece, gibDir, &pinst);
		if (piece && !gibPiece)
			G3FX_BloodCloud(piece, z.pieceCapBolt, 400);
	}
	// piece side caps
	if (pinst) {
		AttachCap(this, pinst, z.pieceCapBolt, z);
		if (z.pieceCap) {
			pinst->SetPartOnOff(z.pieceCap, true);
			if (cBloodSkin && cMaterial)
				pinst->SetSkin(cMaterial, cBloodSkin, z.pieceCap);
		}
	}
	// hand-over of the zone chain (with or without a piece, SPEC-AMEND-006 G3.16/G3.17); the sever group is walked
	// only with a piece; a head group's entries are processed without one (head explosion, gib head)
	HandOverChain(this, zi, inst, pinst, piece, pinst ? 2 : 0);
	if (head && !pinst)
		for (int k = 0; k < z.nsever; k++)
			if (z.sever[k].kind == 0)
				inst->SetPartOnOff(z.sever[k].id, false);
			else if (z.sever[k].kind == 1)
				MoveBoltOns(this, inst, NULL, z.sever[k].id, NULL);
	// body side, always: body cap with `c_blood`, the cap bolt-on at the body-side cap bolt
	if (z.bodyCap) {
		inst->SetPartOnOff(z.bodyCap, true);
		if (cBloodSkin && cMaterial)
			inst->SetSkin(cMaterial, cBloodSkin, z.bodyCap);
	}
	if (z.bodyCapBolt) {
		AttachCap(this, inst, z.bodyCapBolt, z);
		if (!gibPiece)
			G3FX_BloodCloud(body->ent, z.bodyCapBolt, 100);
	}
	// rule 3.7 (rule-3 severing and head explosions only; a gib has no such step: there the owner part goes only
	// through the sever-group walk, SPEC-AMEND-006 G3.17)
	if (!gibPiece) {
		if (z.owner)
			inst->SetPartOnOff(z.owner, false);
		if (z.woundMesh)
			inst->SetPartOnOff(z.woundMesh, false);
		if (exitz >= 0 && exitz < numZones && zones[exitz].woundMesh)
			inst->SetPartOnOff(zones[exitz].woundMesh, false);
	}
	if (piece) {
		FinishPiece(this, piece, pinst, zi, gibPiece);
		if (gibPiece) {
			if (!keep && (g3_gibPieces > 1 || g3_gibCounter > 2)) {
				G_FreeEdict(piece);
				piece = NULL;
			}
			g3_gibPieces++;
		}
	}
	if (zi == GZ_HEAD_FRONT || zi == GZ_HEAD_BACK || zi == GZ_NECK)
		voiceDisabled = true;
	return piece;
}

void G3Gore::HeadExplosion(int zi, const vec3_t hitPos)
{
	IGhoulInst *inst = body->Inst();
	voiceDisabled = true;
	body->muted = true;
	body->SetExpression(EXPR_DEAD, 1.0f, false);
	GhoulID mouth = G3G_Part(inst, "sbolt_mouth");
	if (mouth) {
		vec3_t ext = {8, 8, 8};
		G3FX_Debris(hitPos, vec3_origin, G3_irand(1, 3), 1, 32, ext, 35);
		G3FX_Debris(hitPos, vec3_origin, G3_irand(3, 6), 4, 32, ext, 35);
		if (!g3locks.blood)
			G3FX_OnBolt("gore/headcloud", body->ent, mouth);
		G3_Sound(body->ent, CHAN_BODY, "impact/gore/headexp.wav", 0.6f, ATTN_NORM);
	}
	Sever(zi, -1, false, vec3_origin, false);
}

// ---------------------------------------------------------------------------------------------------------------
// severed pieces (§6)
// ---------------------------------------------------------------------------------------------------------------
static void PieceTouch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)other;
	(void)plane;
	(void)surf;
	if (self->count)
		return;
	self->count = 1;
	G3_Sound(self, CHAN_BODY, "impact/gore/limbfall.wav", 0.6f, ATTN_NORM);
	self->touch = NULL; // plays once, then removes itself (G3.18)
}

static void GibPieceTouch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)surf;
	// gib pieces only send dangly blood; no `limbfall` sound (observed black box: DM self-rocket gib, two pieces
	// touching the world, no `impact/gore/limbfall.wav`)
	if (other == world && VectorLength(self->velocity) > 2 && !g3locks.blood) {
		vec3_t d = {0, 0, -1};
		if (plane)
			VectorNegate(plane->normal, d);
		G3FX_At("gore/danglyblood", self->s.origin, d);
	}
	VectorClear(self->avelocity); // every touch stops the spin, whatever the toucher (SPEC-AMEND-006 G3.18)
}

static void PieceThink(edict_t *self)
{
	if (self->dietime && level.time >= self->dietime) {
		G_FreeEdict(self);
		return;
	}
	self->nextthink = level.time + 0.1f;
}

static void PieceDie(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	(void)inflictor;
	(void)attacker;
	(void)damage;
	(void)point;
	self->health = 300; // pieces are not destroyed by damage (lifetime belongs to the limb AI)
}

// ---------------------------------------------------------------------------------------------------------------
// gibbing (§8) and Dekker split (§9)
// ---------------------------------------------------------------------------------------------------------------
void G3Gore::Gib(const G3Hit &hit)
{
	if (g3locks.sever || body->gibbed)
		return;
	IGhoulInst *inst = body->Inst();
	edict_t *ent = body->ent;
	g3_gibCounter++;
	g3_gibPieces = 0;
	body->muted = true;
	// a deathmatch player's body has no current record (the player code plays its sequences): it counts as
	// standing/moving (observed black box: the gib pieces of a player carry `lb_sautofire_end_n_n`)
	if (IsHuman(body) && (body->cur ? body->cur->cls <= ANIMCLS_MOVE : body->ent->client != NULL)) {
		vec3_t f, l, u;
		G3_EntityAxes(ent, f, l, u);
		const G3Anim *fwd = Anim_Human("lf_sautofire_end_n_n");
		const G3Anim *bk = Anim_Human("lb_sautofire_end_n_n");
		if (DotProduct(hit.dir, f) > 0 && body->HasAnim(fwd))
			body->SetAnim(fwd, true);
		else
			body->SetAnim(bk, true);
	}
	if (!G3_Cinematic() && g3hooks.statistic)
		g3hooks.statistic(0);
	if (!g3locks.blood && g3_gibCounter < 4) {
		if (hit.dflags & DMGF_MICROWAVE) {
			if (inst)
				inst->SetTint(1, 0.3f, 0, 1);
			G3FX_At("gore/mpgsplatter", ent->s.origin);
		} else
			G3FX_At("gore/hugeburst2", ent->s.origin);
	}
	body->SetExpression(EXPR_DEAD, 1.0f, false);
	int gone = 0;
	if (IsHuman(body) && inst) {
		static const int limbs[9] = {GZ_HEAD_FRONT, GZ_ARM_UPPER_LEFT, GZ_ARM_UPPER_RIGHT, GZ_ARM_LOWER_LEFT,
			GZ_ARM_LOWER_RIGHT, GZ_LEG_UPPER_LEFT_FRONT, GZ_LEG_UPPER_RIGHT_FRONT, GZ_LEG_LOWER_LEFT,
			GZ_LEG_LOWER_RIGHT};
		for (int i = 0; i < 9; i++) {
			int zi = limbs[i];
			if (!OwnerShown(zi)) {
				gone++;
				continue;
			}
			if (G3_irand(0, 1) != 0)
				continue;
			vec3_t d = {hit.dir[0] + G3_flrand(-1.5f, 1.5f), hit.dir[1] + G3_flrand(-1.5f, 1.5f),
				hit.dir[2] + G3_flrand(1, 2)};
			if (zones[zi].bodyCap || zones[zi].bodyCapBolt) // SPEC-AMEND-006 G3.17 2.
				Sever(zi, -1, true, d, false);
			gone++;
		}
		if (G3_irand(0, 2) == 0 && OwnerShown(GZ_CHEST_FRONT)) {
			vec3_t d = {hit.dir[0] + G3_flrand(-1.5f, 1.5f), hit.dir[1] + G3_flrand(-1.5f, 1.5f),
				hit.dir[2] + G3_flrand(1, 2)};
			GibChest(d, false);
		}
	}
	if (gone > 6 || (hit.dflags & (DMGF_MICROWAVE | DMGF_EXPLOSIVE))) {
		body->gibbed = true;
		ent->solid = SOLID_NOT;
		gix.linkentity(ent);
		G3FX_OnEnt("gore/gibbody", ent);
		if (inst) {
			inst->SetOnOff(false, G3_Time());
			G3G_RemoveAllChildren(inst);
		}
	}
}

// the chest group leaves as a gib piece (gibbing / Dekker split); returns the piece
edict_t *G3Gore::GibChest(const vec3_t dir, bool keep)
{
	IGhoulInst *inst = body->Inst();
	if (!inst)
		return NULL;
	edict_t *piece = NULL;
	IGhoulInst *pinst = NULL;
	G3Zone &z = zones[GZ_CHEST_FRONT];
	if (keep || (g3_gibCounter < 5 && g3_gibPieces < 2))
		piece = SpawnPiece(this, GZ_CHEST_FRONT, true, dir, &pinst);
	if (pinst) {
		AttachCap(this, pinst, z.pieceCapBolt, z);
		if (z.pieceCap) {
			pinst->SetPartOnOff(z.pieceCap, true);
			if (cBloodSkin && cMaterial)
				pinst->SetSkin(cMaterial, cBloodSkin, z.pieceCap);
		}
	}
	// as every gib limb (SPEC-AMEND-006 G3.17): hand-over always, the sever-group walk only with a piece, no
	// separate owner hide
	HandOverChain(this, GZ_CHEST_FRONT, inst, pinst, piece, pinst ? 2 : 0);
	if (z.bodyCap) {
		inst->SetPartOnOff(z.bodyCap, true);
		if (cBloodSkin && cMaterial)
			inst->SetSkin(cMaterial, cBloodSkin, z.bodyCap);
	}
	AttachCap(this, inst, z.bodyCapBolt, z);
	if (piece) {
		FinishPiece(this, piece, pinst, GZ_CHEST_FRONT, true);
		if (!keep && (g3_gibPieces > 1 || g3_gibCounter > 2)) {
			G_FreeEdict(piece);
			piece = NULL;
		}
		g3_gibPieces++;
	}
	return piece;
}

void Gore_DekkerSplit(edict_t *target)
{
	G3Body *b = Body_Of(target);
	if (!b || !b->gore || g3locks.sever)
		return;
	g3_gibCounter = 0;
	g3_gibPieces = 0;
	vec3_t up = {0, 0, 1};
	edict_t *p = b->gore->GibChest(up, true);
	if (p)
		p->nextthink = level.time + 60.0f;
}

// ---------------------------------------------------------------------------------------------------------------
// stuck knives (§10)
// ---------------------------------------------------------------------------------------------------------------
void G3Gore::StickKnife(int zi, const G3Hit &hit)
{
	IGhoulInst *inst = body->Inst();
	if (!inst || zi < 0 || zi >= numZones)
		return;
	GhoulID bolt = zones[zi].pieceCapBolt;
	if (!bolt) {
		const char *n;
		switch (zi) {
		case GZ_SHLDR_RIGHT_FRONT:
		case GZ_SHLDR_RIGHT_BACK: n = "gbolt_cap_rbicep_chest_tight"; break;
		case GZ_SHLDR_LEFT_FRONT:
		case GZ_SHLDR_LEFT_BACK: n = "gbolt_cap_lbicep_chest_tight"; break;
		case GZ_NECK: n = G3G_Part(inst, "abolt_chest") ? "abolt_chest" : "abolt_hip_r"; break;
		case GZ_FOOT_RIGHT: n = "sbolt_heel_r"; break;
		case GZ_FOOT_LEFT: n = "sbolt_heel_l"; break;
		default: n = "abolt_hip_r"; break;
		}
		bolt = G3G_Part(inst, n);
	}
	if (!bolt)
		return;
	IGhoulObject *o = G3G_ModelObject("Enemy/bolt", "w_knife");
	IGhoulInst *k = G3G_AttachNew(inst, bolt, o, "tip", 1.0f);
	if (!k)
		return;
	// point the knife along its flight direction (pitch 90 degrees relative to the bolt)
	Matrix4 bm;
	if (G3G_BoltMatrix(inst, bolt, &bm, 5, true)) {
		vec3_t ld;
		G3_WorldDirToEntity(body->ent, hit.dir, ld);
		(void)ld; // the exact orientation formula is not specified beyond "pitch 90"; keep the bolt alignment
	}
	body->AddStuckKnife(k);
}

void G3Gore::ReleaseKnives(IGhoulInst *only)
{
	body->ReleaseStuckKnives(only);
}

// ---------------------------------------------------------------------------------------------------------------
// bolt-ons knocked off by hits (§11)
// ---------------------------------------------------------------------------------------------------------------
struct AccessoryRule {
	const char *name;
	bool anyHit, swapHead, absorbs;
};
static const AccessoryRule accessories[] = {
	{"acc_antenna", 0, 0, 0}, {"acc_backpack_low", 0, 0, 1}, {"acc_backpack_smll", 0, 0, 1}, {"acc_box_hip", 0, 0, 1},
	{"acc_box_hip_lrg", 0, 0, 1}, {"acc_box_hip_smll", 0, 0, 1}, {"acc_ecto_glasses", 1, 0, 0},
	{"acc_faceplate", 0, 0, 0}, {"acc_faceplate2", 0, 0, 0}, {"acc_gas_mask2", 1, 0, 0}, {"acc_glasses", 1, 0, 0},
	{"acc_hat_baseball", 0, 1, 0}, {"acc_hat_beret", 0, 1, 0}, {"acc_hat_flop2", 0, 1, 0},
	{"acc_hat_general", 0, 1, 0}, {"acc_hat_general2", 0, 1, 0}, {"acc_hat_hard", 0, 1, 1},
	{"acc_hat_tall", 0, 1, 0}, {"acc_hat_top", 0, 1, 0}, {"acc_hat_turbin", 0, 1, 0}, {"acc_helmet", 0, 1, 1},
	{"acc_holster", 0, 0, 0}, {"acc_machette", 0, 0, 0}, {"acc_nightvision2", 0, 0, 0}, {"acc_sword", 0, 0, 0},
	{"w_assault_rifle", 0, 0, 1}, {"w_autoshotgun", 0, 0, 1}, {"w_bat", 0, 0, 1}, {"w_briefcase", 1, 0, 1},
	{"w_flamethrower", 0, 0, 1}, {"w_kantana", 0, 0, 1}, {"w_knife", 0, 0, 1}, {"w_machete", 0, 0, 1},
	{"w_machinegun", 0, 0, 1}, {"w_machinepistol", 0, 0, 1}, {"w_mpg", 0, 0, 1}, {"w_pistol1", 0, 0, 1},
	{"w_pistol2", 0, 0, 1}, {"w_rocket", 0, 0, 1}, {"w_sack", 1, 0, 1}, {"w_shotgun", 0, 0, 1},
	{"w_sniperrifle", 0, 0, 1}, {NULL, 0, 0, 0}};

static const AccessoryRule *FindAccessory(const char *name)
{
	for (int i = 0; accessories[i].name; i++)
		if (!Q_stricmp(accessories[i].name, name))
			return &accessories[i];
	return NULL;
}

static void SwapHelmetHead(G3Gore *g)
{
	IGhoulInst *inst = g->body->Inst();
	auto swap = [&](const char *from, const char *to) -> bool {
		GhoulID a = G3G_Part(inst, from), b = G3G_Part(inst, to);
		if (!a || !b || !inst->IsPartOn(a))
			return false;
		inst->SetPartOnOff(a, false);
		inst->SetPartOnOff(b, true);
		return true;
	};
	if (g->body->family == BODY_ECTO) {
		if (swap("_headhelmet", "_headbald")) {
			GhoulID b = G3G_Part(inst, "_headbald");
			g->zones[GZ_HEAD_FRONT].owner = g->zones[GZ_HEAD_BACK].owner = b;
			g->zones[GZ_HEAD_FRONT].woundMesh = G3G_Part(inst, "gz_headbald_f");
			g->zones[GZ_HEAD_BACK].woundMesh = G3G_Part(inst, "gz_headbald_b");
		}
		return;
	}
	if (swap("_helmethead", "_baldhead")) {
		g->zones[GZ_HEAD_BACK].owner = G3G_Part(inst, "_baldhead");
		g->zones[GZ_HEAD_BACK].woundMesh = G3G_Part(inst, "gz_baldhead_b");
	}
	if (swap("_helmetface", "_comface")) {
		g->zones[GZ_HEAD_FRONT].owner = G3G_Part(inst, "_comface");
		g->zones[GZ_HEAD_FRONT].woundMesh = G3G_Part(inst, "gz_comface");
	}
}

static void DroppedThink(edict_t *self);

// Throw a copy of a bolt-on model off the character (rule 11.3); returns the new entity.
static edict_t *ThrowBoltOn(G3Gore *g, IGhoulInst *c, const G3Hit &hit, bool weapon)
{
	edict_t *ch = g->body->ent;
	IGhoulObject *o = c->GetGhoulObject();
	vec3_t vel, toChar;
	VectorSubtract(ch->s.origin, hit.point, toChar);
	VectorNormalize(toChar);
	for (int i = 0; i < 3; i++)
		vel[i] = 150.0f * hit.dir[i] + 150.0f * toChar[i];
	vel[2] += 300.0f;
	if (weapon) {
		int w = Body_WeaponNumber(G3G_ModelName(o));
		// players' weapons are dropped by the player module (SPEC-790 death drop); unknown models drop nothing
		if (ch->client || w <= 0)
			return NULL;
		if (g3hooks.dropWeapon)
			return g3hooks.dropWeapon(ch, w, ch->s.origin, vel);
		edict_t *e = DropWeaponItem(ch, w, 0);
		if (e) {
			VectorCopy(ch->s.origin, e->s.origin);
			VectorCopy(vel, e->velocity);
			e->s.angles[YAW] = G3_flrand(-180, 180);
			e->g3flags |= G3F_KNIFE_PICKUP & 0;
			e->dietime = level.time + 5.0f; // removal check after 5 s (lifetime 30 s)
		}
		return e;
	}
	edict_t *e = G_Spawn();
	if (!e)
		return NULL;
	IGhoulInst *ni = G3G_NewInstFor(o, e);
	if (!ni) {
		G_FreeEdict(e);
		return NULL;
	}
	GhoulID inworld = o->FindPart("inworld");
	if (inworld) {
		ni->SetAllPartsOnOff(false);
		ni->SetPartOnOff(inworld, true);
	}
	GhoulID seq = c->GetPlayingSequence();
	if (seq)
		ni->Play(seq, G3_Time(), 0, true, kPlayLoop, false, false);
	e->s.renderfx = RF_GHOUL;
	e->classname = (char *)"g3_dropped_boltOn";
	VectorCopy(ch->s.origin, e->s.origin);
	VectorCopy(vel, e->velocity);
	e->s.angles[YAW] = G3_flrand(-180, 180);
	e->movetype = 9;
	e->solid = SOLID_TRIGGER;
	VectorSet(e->mins, -1, -1, -1);
	VectorSet(e->maxs, 1, 1, 1);
	e->health = 10000;
	e->think = DroppedThink;
	e->nextthink = level.time + 5.0f;
	e->dietime = level.time + 5.0f;
	gix.linkentity(e);
	return e;
}

// removal check: removed as soon as no player within 800 units looks at it (checked every second)
static void DroppedThink(edict_t *self)
{
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (!p->inuse || !p->client)
			continue;
		vec3_t d, f;
		VectorSubtract(self->s.origin, p->s.origin, d);
		if (VectorLength(d) > 800)
			continue;
		if (!gix.inPVS(p->s.origin, self->s.origin))
			continue;
		VectorNormalize(d);
		AngleVectors(p->client->v_angle, f, NULL, NULL);
		if (DotProduct(f, d) >= 0.2f) {
			self->nextthink = level.time + 1.0f;
			return;
		}
	}
	G_FreeEdict(self);
}

// Throw a bolt-on off the body (weapon pickup or plain model, rule 11.2/11.3) and remove it from the model.
void G3Gore::DropBoltOn(IGhoulInst *c, const G3Hit &hit, bool weapon)
{
	IGhoulInst *inst = body->Inst();
	if (!c || !inst)
		return;
	if (!weapon || !body->NoDrop())
		ThrowBoltOn(this, c, hit, weapon);
	IGhoulInst *parent = c->GetParent();
	G3G_RemoveChild(parent ? parent : inst, c);
}

int G3Gore::KnockOff(IGhoulInst *c, int damage, const G3Hit &hit, bool anyHitOnly)
{
	IGhoulInst *inst = body->Inst();
	edict_t *ch = body->ent;
	if (!c || !inst)
		return damage;
	if (ch->client)
		return damage;                           // rule 11.6 is handled by the player path
	if (G3_ClassIs(ch, "m_x_mmerc") || !body->AllowsDrops())
		return damage;
	const char *name = G3G_ModelName(c->GetGhoulObject());
	if (!Q_stricmp(name, "acc_tanks")) {
		if (!anyHitOnly)
			FuelTankExplode(c, hit);
		return damage;
	}
	const AccessoryRule *r = FindAccessory(name);
	if (!r)
		return damage;
	if (!r->anyHit && (anyHitOnly || damage < 2))
		return damage;
	// the SPEC-790 damage predicate: w_pistol1 and w_knife are never knocked off a body
	if (!Q_stricmp(name, "w_pistol1") || !Q_stricmp(name, "w_knife"))
		return r->absorbs ? 0 : damage;
	bool weapon = !Q_strncasecmp(name, "w_", 2);
	GhoulID bolt = 0;
	int n = G3G_NumChildren(inst);
	for (int i = 0; i < n; i++) {
		GhoulID b;
		if (G3G_Child(inst, i, &b) == c) {
			bolt = b;
			break;
		}
	}
	int hand = weapon ? body->HandBolt(bolt) : -1;
	DropBoltOn(c, hit, weapon);
	if (hand >= 0)
		body->ForgetHand(hand);
	if (r->swapHead)
		SwapHelmetHead(this);
	if (r->absorbs) {
		edict_t *a = hit.attacker;
		if (a && a->client && a->client->inv && a->client->inv->CurrentWeapon() == 6)
			return damage; // sniper rifle shots pass on with full damage
		return 0;
	}
	return damage;
}

void G3Gore::FuelTankExplode(IGhoulInst *tank, const G3Hit &hit)
{
	IGhoulInst *inst = body->Inst();
	edict_t *ch = body->ent;
	G3G_RemoveChild(inst, tank);
	ch->health = 0;
	Dmg_RadiusLinear(ch, ch, 250, NULL, 175, MOD_EXPLOSIVE, DMGF_FIRE);
	edict_t *list[MAX_EDICTS];
	vec3_t mins = {ch->s.origin[0] - 175, ch->s.origin[1] - 175, ch->s.origin[2] - 175};
	vec3_t maxs = {ch->s.origin[0] + 175, ch->s.origin[1] + 175, ch->s.origin[2] + 175};
	int n = gix.BoxEdicts(mins, maxs, list, MAX_EDICTS, AREA_SOLID);
	for (int i = 0; i < n; i++)
		if (list[i]->takedamage && g3hooks.setOnFire)
			g3hooks.setOnFire(list[i], ch, 4.0f);
	if (g3hooks.explosionNoise)
		g3hooks.explosionNoise(ch->s.origin, 200, 200);
	G3FX_OnEnt("weapons/world/airexplode", ch);
	if (g3hooks.cameraShake)
		g3hooks.cameraShake(hit.point, 100, 200, 30);
}

// ---------------------------------------------------------------------------------------------------------------
// character zone armour (SPEC-720) and savegame (§ "Savegame data")
// ---------------------------------------------------------------------------------------------------------------
int Gore_ArmourPoints(G3Body *b)
{
	if (b->family != BODY_MESO_ARMOURED)
		return 300;
	static const int a[5] = {75, 99, 150, 150, 300};
	return a[Dmg_Toughness()];
}

static bool InConfig(int type, int z)
{
	switch (type) {
	case 1:
		return (z >= 3 && z <= 8) || z == 13 || z == 14;
	case 2:
	case 6:
		return (z >= 3 && z <= 25);
	case 3:
	case 7:
		return z >= 0 && z <= 25;
	}
	return false;
}

void G3Gore::SetArmour(int type, int A)
{
	if (numZones < 26)
		return;
	switch (type) {
	case 0:
		for (int i = 0; i < 26; i++)
			zones[i].damage = 0;
		break;
	case 1:
	case 2:
	case 3:
	case 6:
	case 7: {
		int pts = type == 3 ? A * 4 : (type == 6 ? (int)(A * 0.3f) : A);
		for (int i = 0; i < 26; i++)
			if (InConfig(type, i))
				zones[i].damage -= pts;
		break;
	}
	default:
		return;
	}
	armourType = type;
}

void G3Gore::ClearArmour(void)
{
	for (int i = 0; i < numZones; i++)
		if (InConfig(armourType, i) && zones[i].damage < 0)
			zones[i].damage = 0;
	armourType = 0;
}

int G3Gore::RemainingArmour(void)
{
	int sum = 0;
	for (int i = 0; i < numZones; i++)
		if (InConfig(armourType, i) && zones[i].damage < 0)
			sum -= zones[i].damage;
	return sum;
}

void G3Gore::SaveWrite(void)
{
	int v[G3_MAX_ZONES];
	for (int i = 0; i < numZones; i++)
		v[i] = zones[i].damage;
	if (gix.WriteChunk)
		gix.WriteChunk('A' << 24 | 'I' << 16 | 'G' << 8 | 'Z', v, numZones * 4);
}

void G3Gore::SaveRead(const int *v, int count)
{
	for (int i = 0; i < numZones && i < count; i++) {
		zones[i].damage = v[i];
		zones[i].level = LevelOf(v[i]);
	}
}
