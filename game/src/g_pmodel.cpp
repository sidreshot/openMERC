// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - deathmatch player models: the level-start model list (.gpl / .gpm), the per-player
// model set-up (Ghoul instance, skins, parts, bolt-ons, body) and the third-person animation choice.
// Models, instances and bolt-ons go through the Ghoul glue (SPEC-791, G9); bodies are G3's (SPEC-722).
// Implements: SPEC-780 16, SPEC-010 D (player-model description use), SPEC-121 (.gpl list)

#include "g_local.h"
#include "ghoulglue/gg_ghoul.h" /* G9: model records, instances, bolt-ons */
#include "damage/damage_api.h"  /* G3: player bodies */
#include "modes/g_modes.h"       /* G9: carried CTF flag */

using namespace ghoul;

#define MAX_PMODELS 128
#define MAX_PBOLTONS 128

struct PBolton {
	char file[100];             // relative to ghoul/pmodels/
	IGhoulPlayerModel *desc;
	GGModel *rec;               // game model record (NULL: menu-only bolt-on)
	char skin[100];             // first skin of the .gbm ("" = none)
};

struct PModel {
	char name[64];
	IGhoulPlayerModel *desc;
	GGModel *rec;
	char dir[100], base[64], list[64], face[64], team[64], sounddir[100];
	bool female;
};

static PModel pmodels[MAX_PMODELS];
static int npmodels;
static int pmodel_default = -1;
static PBolton pboltons[MAX_PBOLTONS];
static int npboltons;

static void PM_Touch(const char *path)
{
	IGhoulSystem *gs = GhoulSys();
	if (gs)
		gs->TouchFile(path);
}

static void PM_FreeDescs(void)
{
	for (int i = 0; i < npmodels; i++)
		if (pmodels[i].desc)
			pmodels[i].desc->Destroy(1);
	for (int i = 0; i < npboltons; i++)
		if (pboltons[i].desc)
			pboltons[i].desc->Destroy(1);
	memset(pmodels, 0, sizeof(pmodels));
	memset(pboltons, 0, sizeof(pboltons));
	npmodels = npboltons = 0;
	pmodel_default = -1;
}

struct PModel;
static void PM_BuildRecord(PModel *pm);
static void PM_PrecacheBody(PModel *pm);

// a skin of a bolt-on record: on the named material, else on the material(s) the object has (captures
// suite1/rec jpndm2: `gina_guns.gbm` names material `acc_gina_guns`, the object's only material is
// `ACC_HOLSTER_RED`, and the original's record carries skin ACC_HOLSTER_RED; likewise box_rick, brian_beret,
// joe_head, knife_jim)
static void PM_BoltonSkin(GGModel *rec, const char *material, const char *skin)
{
	IGhoulObject *o = GG_Object(rec);
	if (o && material && material[0] && o->FindMaterial(material))
		GG_RegisterSkin(rec, material, skin);
	else
		GG_RegisterSkinAnyMaterial(rec, skin);
}

// a bolt-on description (`boltons/<name>.gbm`): file recorded as used, its game model registered.
// `modelskin` = the last skin entry of the player model being loaded: a .gbm without skins gets a record with
// that skin (observed, OPEN_QUESTIONS G18 36: antenna.gbm on iceman → ACC_ANTENNA record with F_SIBTROOP2, sword.gbm
// on ninja → ACC_SWORD with H_NINJA)
static PBolton *PM_Bolton(const char *file, const char *modelskin = NULL)
{
	for (int i = 0; i < npboltons; i++)
		if (!Q_stricmp(pboltons[i].file, file))
			return &pboltons[i];
	if (npboltons >= MAX_PBOLTONS || !gix.NewPlayerModelInfo)
		return NULL;
	PBolton *b = &pboltons[npboltons];
	memset(b, 0, sizeof(*b));
	Com_sprintf(b->file, sizeof(b->file), "%s", file);
	PM_Touch(va("ghoul/pmodels/%s", file));
	b->desc = gix.NewPlayerModelInfo(file);
	if (!b->desc || !b->desc->IsLoaded()) {
		if (b->desc)
			b->desc->Destroy(1);
		b->desc = NULL;
		npboltons++;
		return b;
	}
	PlayerSkinEntry se;
	int ns = b->desc->NumSkins();
	if (ns > 0 && b->desc->GetSkinEntry(1, &se))
		Com_sprintf(b->skin, sizeof(b->skin), "%s", se.skin);
	else if (modelskin && modelskin[0])
		Com_sprintf(b->skin, sizeof(b->skin), "%s", modelskin);
	const char *dir = b->desc->GetModelDir();
	const char *list = b->desc->GetSequenceList();
	const char *game = b->desc->GetBaseModel();
	// the game model (token 3) is the record's base sequence and its list: the original does not load the
	// sequence list of token 4 for a bolt-on (nightvision.gbm: `acc_nightvision.gsq` with acc_nightvision and
	// fade_nightvision is never registered, only ACC_NIGHTVISION2; captures suite1, 43 DM maps). A .gbm with skins
	// gets a record for its first skin with exactly those skins registered; one without skins a record with the
	// player model's last skin (above); only without either a record with all skins (SPEC-791 1)
	(void)list;
	if (dir && dir[0] && game && game[0]) {
		if (b->skin[0]) {
			b->rec = GG_FindModel(dir, game, false, b->skin, NULL);
			if (b->rec && !GG_Prepared(b->rec)) {
				if (ns > 0) {
					for (int k = 1; k <= ns; k++)
						if (b->desc->GetSkinEntry(k, &se))
							PM_BoltonSkin(b->rec, se.material, se.skin);
				} else
					PM_BoltonSkin(b->rec, NULL, b->skin);
				GG_Prepare(b->rec);
			}
		} else
			b->rec = GG_FindModel(dir, game, true, NULL, NULL);
	}
	npboltons++;
	return b;
}

// death and pain voice files of a model's sound directory (the list of SPEC-722 voices)
static void PM_PrecacheVoices(const char *dir)
{
	static const struct { const char *e; int n; } gv[] = {{"Burn", 1}, {"Choke", 2}, {"Crotch", 2}, {"Hit", 3},
		{"Scream", 8}, {"Writhe", 7}, {"Limp", 3}, {"Getup", 1}, {"Disarm", 1}};
	if (!dir || !dir[0])
		return;
	for (auto &g : gv)
		for (int k = 1; k <= g.n; k++)
			gi.soundindex(va("%s/%s%d.wav", dir, g.e, k));
}

static void PM_LoadModel(const char *name)
{
	if (npmodels >= MAX_PMODELS)
		return;
	for (int i = 0; i < npmodels; i++)
		if (!Q_stricmp(pmodels[i].name, name))
			return;
	IGhoulPlayerModel *d = gix.NewPlayerModelInfo ? gix.NewPlayerModelInfo(va("%s.gpm", name)) : NULL;
	if (!d)
		return;
	if (!d->IsLoaded()) {
		d->Destroy(1);
		return;
	}
	PModel *pm = &pmodels[npmodels];
	memset(pm, 0, sizeof(*pm));
	Com_sprintf(pm->name, sizeof(pm->name), "%s", name);
	pm->desc = d;
	Com_sprintf(pm->dir, sizeof(pm->dir), "%s", d->GetModelDir());
	Com_sprintf(pm->base, sizeof(pm->base), "%s", d->GetBaseModel());
	Com_sprintf(pm->list, sizeof(pm->list), "%s", d->GetSequenceList());
	Com_sprintf(pm->face, sizeof(pm->face), "%s", d->GetFacePrefix());
	d->GetTeam(pm->team);
	d->GetSoundDir(pm->sounddir);
	pm->female = !Q_stricmp(pm->base, "fem_play");

	PM_Touch(va("ghoul/pmodels/%s.gpm", name));
	PM_Touch(va("ghoul/pmodels/portraits/%s.m32", name));
	if (pm->team[0]) {
		PM_Touch(va("ghoul/pmodels/teamicons/%s.m32", pm->team));
		PM_Touch(va("pics/menus/teamicons/%s.m32", pm->team));
	}
	PM_Touch(va("menus/players/%s.rmf", name));
	PM_PrecacheVoices(pm->sounddir);
	// Each model in list order: its record, then its bolt-on records in reverse file order, then the body precache
	// temporary (SPEC-780 16, AMEND-005 G18.25 rule 1). Observed record order (captures suite1/rec jpndm2,
	// OPEN_QUESTIONS G18 36): dekker's record 20, G_GUTS_LRG 21 (the first body precache), gimp 22, shadow 23,
	// then shadow's ACC_NIGHTVISION2 24 and ACC_BACKPACK_LRG 25 (.gpm order: lrgbackpackblk, nightvision).
	PM_BuildRecord(pm);
	PlayerSkinEntry last;
	int ns = d->NumSkins();
	const char *lastskin = ns > 0 && d->GetSkinEntry(ns, &last) ? last.skin : NULL;
	for (int i = d->NumBoltons(); i >= 1; i--) {
		PlayerBoltonEntry be;
		if (d->GetBoltonEntry(i, &be))
			PM_Bolton(be.file, lastskin);
	}
	PM_PrecacheBody(pm);
	if (pmodel_default < 0)
		pmodel_default = npmodels;
	npmodels++;
}

// the skins a player model needs on its record (SPEC-780 16)
static void PM_RegisterSkins(GGModel *rec, PModel *pm)
{
	IGhoulPlayerModel *d = pm->desc;
	int ns = d->NumSkins();
	for (int k = 1; k <= ns; k++) {
		PlayerSkinEntry se;
		if (d->GetSkinEntry(k, &se))
			GG_RegisterSkin(rec, se.material, se.skin);
	}
	// gore-zone skins on material `gz`, cap skins on material `c` (the materials the gore code uses, SPEC-721)
	static const char *const gz[] = {"gz_sing1", "gz_sing2", "gz_sing3", "gz_sing_k", "gz_multi1", "gz_multi2",
		"gz_multi3", "gz_multi_k", "gz_mass1", "gz_mass2", "gz_mass3", "gz_mass_k", NULL};
	for (int i = 0; gz[i]; i++)
		GG_RegisterSkin(rec, "gz", gz[i]);
	GG_RegisterSkin(rec, "c", "c_blood");
	GG_RegisterSkin(rec, "c", "c_black");
	// face skins: prefix + expression suffix; with the default mskins_expression_limit the original registers
	// n, b, d and p (capture orig_walk)
	if (pm->face[0]) {
		static const char *const fs[] = {"n", "b", "d", "p", NULL};
		for (int i = 0; fs[i]; i++)
			GG_RegisterSkin(rec, "f", va("%s%s", pm->face, fs[i]));
	}
	GG_RegisterSkin(rec, "b", "b_dm_armor");
}

// Records: one per model, registered without the all-skins option (the original registers only the skins the
// player models use: capture orig_walk has no unused character skins and faces with 4 suffixes) and prepared at
// once (preparing a record later than right after its registration breaks the Ghoul signon of this engine: the
// client then receives empty ghoul-string batches forever). Models with the same skins still get records of their
// own: the original has 56 player-model records for the 56 models of everybody.gpl, ghost (F_SIBTROOP) and iceman
// (F_SIBTROOP2) separately (captures suite1/rec jpndm2).
static void PM_BuildRecord(PModel *pm)
{
	GGModel *rec = GG_FindModel(pm->dir, pm->list, false, pm->name, pm->base);
	if (!rec)
		return;
	if (!GG_Prepared(rec)) {
		PM_RegisterSkins(rec, pm);
		GG_Prepare(rec);
	}
	pm->rec = rec;
}

// AMEND-005 G18.25 rule 1: a temporary entity gets a body of the model (precaching the body's resources) and is
// freed again. Records <= maxclients + 8 are not released by a free (SPEC-760 B2), so the first eight stay in use
// (`noclass`, never linked) and later serve as the corpse records (SPEC-780 10). A human body precache sets
// material-flag position 10 (AMEND-005 G18.27). The temporary carries the model's Ghoul instance so that the body
// precache also registers the `sound` notes of the model's sequences (SPEC-722 Precache "then the model events";
// the original registers e.g. `weapons/sniper/cock.wav`, `reload.wav` on every DM map — notes of the rifle
// sequences of meso_player).
static void PM_PrecacheBody(PModel *pm)
{
	edict_t *e = G_Spawn();
	GGInst *oi = pm->rec ? GG_NewInst(pm->rec, e) : NULL;
	if (oi)
		e->ghoulInst = GG_EngineInst(oi);
	Body_CreateForPlayer(e, pm->base);
	Body_Destroy(e);
	G_FreeEdict(e); // removes the instance; records <= maxclients + 8 stay in use
	G_SetMaterialFlag(10, 'y');
}

static bool PM_ReadList(const char *file, bool required)
{
	void *buf = NULL;
	int len = gix.FS_LoadFile(va("%s.gpl", file), &buf, 0);
	if (len <= 0 || !buf) {
		if (required)
			gi.error("Could not open %s.gpl", file);
		return false;
	}
	char *text = (char *)gi.TagMalloc(len + 1, TAG_LEVEL);
	memcpy(text, buf, len);
	text[len] = 0;
	gix.FS_FreeFile(buf);
	char *p = text;
	for (;;) {
		const char *tok = COM_Parse(&p);
		if (!p || !tok[0])
			break;
		PM_LoadModel(tok);
	}
	gi.TagFree(text);
	return true;
}

static void PM_SkinRefresh(edict_t *player)
{
	if (player && player->client && player->client->inv)
		PModel_ArmourChanged(player, player->client->inv->Armor() > 0);
}

// SPEC-780 16: level start in deathmatch
void PModel_LevelStart(void)
{
	g3hooks.playerSkinRefresh = PM_SkinRefresh; // G3: armour changed between none and some (SPEC-720 13)
	PM_FreeDescs();
	if (!deathmatch->value)
		return;
	cvar_t *list = gi.cvar("sv_pmodlistfile", "ghoul/pmodels/everybody", 0);
	cvar_t *alt = gi.cvar("sv_altpmodlistfile", "custom", 0);
	if (!list->string || !list->string[0])
		gi.error("No pmodel file specified!");
	PM_ReadList(list->string, true);
	if (alt->string && alt->string[0])
		PM_ReadList(alt->string, false);
	if (pmodel_default < 0)
		PM_LoadModel("mullins");
}

void PModel_Shutdown(void) { PM_FreeDescs(); }

static int PM_Find(const char *name)
{
	for (int i = 0; i < npmodels; i++)
		if (!Q_stricmp(pmodels[i].name, name))
			return i;
	return -1;
}

// SPEC-780 1, 5, 16: the model a requested `skin` stands for. Usable when it names a model of the level's list
// (case-insensitive; `model` = the requested text as given), else the default model (`model` = its list name).
// `team` = the team the model declares, or its list name when it declares `Noteam`.
// Measured (captures ui_orig2..6, original module): `skin` Slick → model field `Slick`, team `slick`; an unknown or
// empty `skin` → the first model of the list (`dekker`, team `The Order`).
bool PModel_Resolve(const char *skin, char *model, int mlen, char *team, int tlen)
{
	int i = skin && skin[0] ? PM_Find(skin) : -1;
	bool ok = i >= 0;
	if (!ok)
		i = pmodel_default;
	if (i < 0) { // no model list (outside deathmatch or nothing loadable)
		Com_sprintf(model, mlen, "mullins");
		Com_sprintf(team, tlen, "mullins");
		return skin && !Q_stricmp(skin, "mullins");
	}
	PModel *pm = &pmodels[i];
	Com_sprintf(model, mlen, "%s", ok ? skin : pm->name);
	Com_sprintf(team, tlen, "%s", pm->team[0] && Q_stricmp(pm->team, "Noteam") ? pm->team : pm->name);
	return ok;
}

static void PM_AttachBoltons(GGInst *oi, PModel *pm)
{
	IGhoulObject *obj = GG_Object(pm->rec);
	int nb = pm->desc->NumBoltons();
	for (int i = 1; i <= nb; i++) {
		PlayerBoltonEntry be;
		if (!pm->desc->GetBoltonEntry(i, &be))
			continue;
		PBolton *b = PM_Bolton(be.file);
		if (!b || !b->rec)
			continue;
		GhoulID pb = obj->FindPart(be.bolt);
		GhoulID cb = GG_Object(b->rec) ? GG_Object(b->rec)->FindPart(be.boltonBolt) : 0;
		if (pb && cb)
			GG_AddBolt(oi, pb, b->rec, cb, b->skin[0] ? b->skin : NULL, be.scale);
	}
}

// SPEC-780 16: set up a player's model (userinfo change, spawn); deathmatch only
void PModel_Setup(edict_t *ent)
{
	gclient_t *c = ent->client;
	if (!deathmatch->value || !c)
		return;
	gi.soundindex("impact/player/hit1.wav");
	gi.soundindex("impact/player/hit2.wav");
	gi.soundindex("impact/player/hit3.wav");
	gi.soundindex("impact/player/hit4.wav");
	gi.soundindex("impact/player/armor.wav");

	GhoulRemove(ent);
	Body_Destroy(ent);
	int i = PM_Find(c->modelname);
	if (i < 0)
		i = pmodel_default;
	if (i < 0)
		gi.error("No default player model.");
	PModel *pm = &pmodels[i];
	GGInst *oi = pm->rec ? GG_NewInst(pm->rec, ent) : NULL;
	if (!oi && i != pmodel_default && pmodel_default >= 0) {
		pm = &pmodels[pmodel_default];
		oi = pm->rec ? GG_NewInst(pm->rec, ent) : NULL;
	}
	if (!oi)
		gi.error("Couldn't set up default player model.");
	c->pmodel = (int)(pm - pmodels);
	c->female = pm->female;
	IGhoulInst *inst = GG_EngineInst(oi);
	ent->ghoulInst = inst;
	ent->s.renderfx |= RF_GHOUL;

	// skins (material, skin [, part]) and part switches of the .gpm
	int ns = pm->desc->NumSkins();
	for (int k = 1; k <= ns; k++) {
		PlayerSkinEntry se;
		if (pm->desc->GetSkinEntry(k, &se))
			GG_SetSkinInst(inst, pm->dir, pm->base, se.material, se.skin);
	}
	int np = pm->desc->NumPartSwitches();
	for (int k = 1; k <= np; k++) {
		PlayerPartEntry pe;
		if (!pm->desc->GetPartEntry(k, &pe))
			continue;
		if (pe.shown)
			GG_ShowPartInst(inst, pe.part);
		else
			GG_HidePartInst(inst, pe.part);
	}
	PM_AttachBoltons(oi, pm);
	// armour: material `b` gets skin `b_dm_armor` (the armoured chest/hip part names are not specified:
	// OPEN_QUESTIONS G18 26)
	c->armor_shown = c->inv && c->inv->Armor() > 0;
	if (c->armor_shown)
		GG_SetSkinInst(inst, pm->dir, pm->base, "b", "b_dm_armor");

	G3Body *body = Body_CreateForPlayer(ent, pm->base);
	if (body) {
		body->SetVoice(NULL, 1, 7, pm->sounddir);
		body->SetFace(pm->face);
	}
	c->pm_anim[0] = 0;
	c->pm_crouched = (c->ps.pmove.pm_flags & PMF_DUCKED) != 0;
	c->pm_posture_until = 0;
	c->pm_lean = 0;
	c->pm_lean_out = false;
	c->pm_handweapon = -1;
	c->pm_flagteam = 0;
	c->pm_flagbolt = NULL;
	c->pm_flagseq[0] = 0;
	PModel_Animate(ent);
}

// armour went from none to some or back (SPEC-780 16, SPEC-781): rebuild the model
void PModel_ArmourChanged(edict_t *ent, bool armoured)
{
	gclient_t *c = ent->client;
	if (!c || c->armor_shown == armoured)
		return;
	// rebuilt at the end of the frame: this can be called from inside the damage code, which still uses the
	// body that a rebuild destroys
	if (deathmatch->value && ent->ghoulInst)
		c->pm_rebuild = true;
}

// SPEC-780 7: on death in deathmatch the weapon models in both hands are taken off the body
void PModel_Died(edict_t *ent)
{
	G3Body *b = Body_Of(ent);
	if (b) {
		b->ClearHand(0);
		b->ClearHand(1);
	}
	if (ent->client)
		ent->client->pm_handweapon = -1;
}

static const char *PM_WaveAnim(int w)
{
	switch (w) {
	case 2: return "std_hsignalstop_n_a_a";
	case 3: return "std_htaunt_n_a_a";
	case 4: return "std_itouchnuts_n_a_n";
	default: return "std_hsignalgo_n_a_a";
	}
}

// AMEND-005 G18.26: weapon class of the player-model locomotion lists (SPEC-722 attack types): P pistols (and no
// weapon: `meso_player` lacks the unarmed lists), MP machine pistol, M assault rifle / machine gun / flame gun,
// R sniper rifle, S slug thrower / shotgun / microwave gun, L rocket launcher, K knife
enum { WC_P, WC_MP, WC_K, WC_R, WC_M, WC_S, WC_L };

static int PM_WeaponClass(int w)
{
	switch (w) {
	case WP_KNIFE: return WC_K;
	case WP_MPISTOL: return WC_MP;
	case WP_ASSAULT: case WP_MACHINEGUN: case WP_FLAMEGUN: return WC_M;
	case WP_SNIPER: return WC_R;
	case WP_SLUGGER: case WP_SHOTGUN: case WP_MPG: return WC_S;
	case WP_ROCKET: return WC_L;
	default: return WC_P;
	}
}

// the first available entries of `meso_player` per situation and class (AMEND-005 G18.26 table; columns P/MP, K,
// R, M/S, L). Strafe entries hold `%s` for `lt`/`rt`.
enum { LS_WALK_FWD, LS_WALK_BACK, LS_WALK_STRAFE, LS_RUN_FWD, LS_RUN_BACK, LS_RUN_STRAFE, LS_CCH_STILL, LS_CCH_MOVE,
	LS_GO_DOWN, LS_STAND_UP, LS_COUNT };
static const char *const pm_loco[LS_COUNT][5] = {
	{"std_waim_fwd_p_2", "std_waim_fwd_p_2", "std_waim_fwd_r_2", "std_waim_fwd_ms_2", "std_waim_fwd_l_2"},
	{"std_wbackaim_n_p_2", "std_wbackaim_n_p_2", "std_wbackaim_n_r_2", "std_wbackaim_n_ms_2", "std_wbackaim_n_l_2"},
	{"std_waimstraf_%s_p_2", "std_waimstraf_%s_p_2", "std_waimstraf_%s_r_2", "std_waimstraf_%s_ms_2",
	 "std_waimstraf_%s_l_2"},
	{"std_raim_n_p_2", "std_raim_n_p_n", "std_raim_n_mrs_2", "std_raim_n_mrs_2", "std_raim_n_l_2"},
	{"std_rbackaim_n_p_2", "std_waim_fwd_p_2", "std_rbackaim_n_mrs_2", "std_rbackaim_n_mrs_2", "std_rbackaim_n_l_2"},
	{"std_raimstraf_%s_p_cbn", "std_raimstraf_%s_p_cbn", "std_raimstraf_%s_mrs_2", "std_raimstraf_%s_mrs_2",
	 "std_raimstraf_%s_l_2"},
	{"cch_a_fwd_p_2", "cch_a_fwd_p_2", "cch_a_fwd_r_2", "cch_a_fwd_ms_2", "cch_a_fwd_l_2"},
	{"cch_raim_fwd_p_2", "cch_r_n_pk_n", "cch_raim_fwd_mrs_2", "cch_raim_fwd_mrs_2", "cch_raim_n_l_2"},
	{"std_xcrouch_dwn_pk_a", "std_xcrouch_dwn_pk_a", "std_xcrouch_dwn_mrs_2", "std_xcrouch_dwn_mrs_2",
	 "std_xcrouch_dwn_l_2"},
	{"cch_xcrouch_up_pk_a", "cch_xcrouch_up_pk_a", "cch_xcrouch_up_mrs_2", "cch_xcrouch_up_mrs_2",
	 "cch_xcrouch_up_l_2"},
};

static const char *PM_LocoName(int situation, int wclass, const char *side, char *buf, int len)
{
	static const int col[] = {0, 0, 1, 2, 3, 3, 4}; // WC_P, WC_MP, WC_K, WC_R, WC_M, WC_S, WC_L
	Com_sprintf(buf, len, pm_loco[situation][col[wclass]], side);
	return buf;
}

// Locomotion on the ground (AMEND-005 G18.26): facing = entity yaw, movement = horizontal velocity; running while the
// run button counts (held, not exhausted); c = facing . movement: forwards c >= 0.8, backwards c <= -0.8, else
// strafing, `lt` when moving right (right . movement >= 0), `rt` otherwise; crouched: still / moving lists. Standing
// still is §16's idle `std_ialert_n_p_n` (one result of SPEC-722's standing choice).
static const char *PM_Locomotion(edict_t *ent, char *buf, int len)
{
	gclient_t *c = ent->client;
	vec3_t hv = {ent->velocity[0], ent->velocity[1], 0};
	float speed = VectorNormalize(hv);
	bool crouched = (c->ps.pmove.pm_flags & PMF_DUCKED) != 0;
	int wc = PM_WeaponClass(c->inv ? InvCurWeapon(c->inv) : 0);
	if (speed == 0) {
		if (crouched)
			return PM_LocoName(LS_CCH_STILL, wc, "", buf, len);
		// standing still: SPEC-722's standing choice "aiming at the target, level" for a player (who always has a
		// hostile armed target): oracle pistol player `std_a_fwd_p_cb` (capture ui_orig), so P → p_cb and MP →
		// p_2; knife and unarmed keep §16's idle
		static const char *const aim[] = {"std_a_fwd_p_cb", "std_a_fwd_p_2", "std_ialert_n_p_n", "std_a_fwd_r_2",
		                                  "std_a_fwd_ms_2", "std_a_fwd_ms_2", "std_a_fwd_l_2"};
		return aim[wc];
	}
	if (crouched)
		return PM_LocoName(LS_CCH_MOVE, wc, "", buf, len);
	vec3_t f, r, flat = {0, ent->s.angles[YAW], 0};
	AngleVectors(flat, f, r, NULL);
	float cf = DotProduct(f, hv);
	bool run = (c->buttons & BUTTON_RUN) && !(c->ps.pmove.pm_flags & PMF_EXHAUSTED);
	const char *side = DotProduct(r, hv) >= 0 ? "lt" : "rt";
	int sit = cf >= 0.8f ? (run ? LS_RUN_FWD : LS_WALK_FWD)
	        : cf <= -0.8f ? (run ? LS_RUN_BACK : LS_WALK_BACK)
	                      : (run ? LS_RUN_STRAFE : LS_WALK_STRAFE);
	return PM_LocoName(sit, wc, side, buf, len);
}

// SPEC-780 16: a carried CTF flag is `items/ctf_flag` on `abolt_hip_r` (skin by team), `flag_run` while running
// forward, else `flag_hold_idle`. The flag's own bolt is taken as `to_abolt_hip_r` (the bolt naming of SPEC-121).
static void PM_FlagBolt(edict_t *ent, GGInst *oi)
{
	gclient_t *c = ent->client;
	int ft = ent->health > 0 ? GM_CarriedFlag(ent) : 0;
	IGhoulObject *obj = GG_Object(GG_InstModel(oi));
	if (!obj)
		return;
	GhoulID hip = obj->FindPart("abolt_hip_r");
	GGModel *fm = GG_FindModel("items/ctf_flag", "flag_hold_idle", true, NULL, NULL, ft == 0);
	if (ft != c->pm_flagteam) {
		if (hip && c->pm_flagbolt)
			GG_RemoveBoltsOn(oi, hip);
		c->pm_flagbolt = NULL;
		c->pm_flagseq[0] = 0;
		c->pm_flagteam = ft;
		if (ft && hip && fm) {
			GhoulID cb = GG_Object(fm)->FindPart("to_abolt_hip_r");
			if (cb)
				c->pm_flagbolt = GG_AddBolt(oi, hip, fm, cb, ft == 1 ? "ctf_flag_blue" : "ctf_flag_red");
		}
	}
	if (!c->pm_flagbolt || !fm)
		return;
	vec3_t f, r, u, flat = {0, c->v_angle[YAW], 0};
	AngleVectors(flat, f, r, u);
	vec3_t hv = {ent->velocity[0], ent->velocity[1], 0};
	const char *seq = (VectorLength(hv) > 150 && DotProduct(f, hv) > 0) ? "flag_run" : "flag_hold_idle";
	if (strcmp(seq, c->pm_flagseq)) {
		GhoulID id = GG_FindSequence(fm, seq);
		if (id)
			GG_PlayBolt(c->pm_flagbolt, id, level.time);
		Com_sprintf(c->pm_flagseq, sizeof(c->pm_flagseq), "%s", seq);
	}
}

// SPEC-780 16: a grenade, C4, flash-pack or claymore use while standing plays the throw
void PModel_ThrowUsed(edict_t *ent)
{
	if (deathmatch->value && ent && ent->client && !(ent->client->ps.pmove.pm_flags & PMF_DUCKED))
		ent->client->throw_pending = true;
}

// on a ladder: the Quake II test (SPEC-760 keeps Quake II player movement): a ladder brush 1 unit in front of the
// player in the horizontal view direction
static bool PM_OnLadder(edict_t *ent)
{
	vec3_t f, flat = {0, ent->client->v_angle[YAW], 0}, spot;
	AngleVectors(flat, f, NULL, NULL);
	VectorMA(ent->s.origin, 1, f, spot);
	trace_t tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, spot, ent, MASK_PLAYERSOLID);
	return tr.fraction < 1 && (tr.contents & CONTENTS_LADDER);
}

// SPEC-780 16: third-person animation (deathmatch)
// SPEC-780 §16: playback speed class from the speed factor — the player's movement scale (the value G9 also writes
// into the pmove block, SPEC-790 B3/B5/B11): q = 1 ÷ scale; index 12 for q > 1.875, 11 for q > 1.75, … 1 for
// q > 0.5, else 0 (duration multipliers 0.5 … 2.0, SPEC-011); a leg injury forces class 5. Reading of "speed factor"
// as the movement scale: black box `spd_orig` (jpndm2, forward speeds 25–200 units/s at scale 1) never changes the
// player instance's speed index (state field 2 stays at its base), so the factor is not the movement speed itself
// (OPEN_QUESTIONS G18 39).
static int PM_SpeedClass(edict_t *ent)
{
	G3Body *body = Body_Of(ent);
	if (body && (body->LimbInjury() & (LIMB_RLEG | LIMB_LLEG)))
		return 5;
	float scale = GM_MoveScale(ent);
	if (scale <= 0)
		return 12;
	float q = 1.0f / scale;
	for (int i = 12; i >= 1; i--)
		if (q > 0.5f + 0.125f * (i - 1) + 0.0001f)
			return i;
	return 0;
}

static void PM_AnimateChoose(edict_t *ent);

void PModel_Animate(edict_t *ent)
{
	PM_AnimateChoose(ent);
	// with a record chosen (also a hit reaction G3 forced with speed index 4, AMEND-006 G3.19) the playback speed is
	// set as for locomotion, at the end of the player's frame
	if (deathmatch->value && ent->client && ent->ghoulInst && ent->health > 0 && !ent->deadflag) {
		int k = PM_SpeedClass(ent);
		if (ent->ghoulInst->GetSpeed() != k)
			ent->ghoulInst->SetSpeed(k);
	}
}

static void PM_AnimateChoose(edict_t *ent)
{
	gclient_t *c = ent->client;
	if (!deathmatch->value || !c || !ent->ghoulInst)
		return;
	if (c->pm_rebuild) {
		c->pm_rebuild = false;
		if (ent->health > 0) {
			PModel_Setup(ent);
			return;
		}
	}
	GGInst *oi = GG_ObjInstOfEntity(ent);
	if (!oi)
		return;
	G3Body *body = Body_Of(ent);

	// the weapon model in the right hand follows the current weapon (SPEC-722 right-hand rule)
	if (ent->health > 0 && body && c->inv) {
		int w = InvCurWeapon(c->inv);
		if (w != c->pm_handweapon) {
			c->pm_handweapon = w;
			WeaponDesc *d = PlayerWeaponDesc(w);
			int at = d ? *(int *)((uint8_t *)d + WDESC_ATTACK_PRI) : 0;
			body->ClearHand(0);
			if (at > 0)
				body->SetRightHand(at);
		}
	}

	PM_FlagBolt(ent, oi);

	const char *anim;
	char namebuf[48];
	int mode = kPlayLoop;
	vec3_t hv = {ent->velocity[0], ent->velocity[1], 0};
	float hspeed = VectorLength(hv);
	bool crouched = (c->ps.pmove.pm_flags & PMF_DUCKED) != 0;
	if (crouched)
		c->wave_pending = 0; // `wave` is ignored when crouched
	if (ent->health > 0 && body && body->cur && (body->cur->flags & ANIMF_FINISH_CYCLE) && !body->AnimDone()) {
		// SPEC-AMEND-006 G3.19 / AMEND-005 G18.26: the current record is kept while it is not done and carries flag
		// 0x00400000 — every hit reaction G3 forces (`std_sarmunhurt_lt_a_n`: play once, 0.2 s); afterwards this
		// frame's movement or standing choice replaces it (below, also when the chosen name is unchanged)
		return;
	} else if (ent->health <= 0 || ent->deadflag) {
		anim = "lb_sautofire_end_n_n";
		mode = kPlayOnce;
	} else if (c->anim_priority == ANIM_ATTACK && level.time < c->anim_until) {
		c->throw_pending = false; // the throw plays to its end; a wave asked for meanwhile is ignored
		c->wave_pending = 0;
		return;
	} else if (c->throw_pending && !crouched) {
		// Quake II style priorities: the throw (attack) interrupts a wave
		anim = "std_tgrenade_n_a_n";
		c->throw_pending = false;
		c->wave_pending = 0;
		c->anim_priority = ANIM_ATTACK;
		c->anim_until = level.time + GhoulSeqDuration(ent, anim, kPlayOnce);
		mode = kPlayOnce;
	} else if (c->anim_priority == ANIM_WAVE && level.time < c->anim_until) {
		c->wave_pending = 0; // the wave plays to its end
		c->throw_pending = false;
		return;
	} else if (c->wave_pending && !crouched) {
		anim = PM_WaveAnim(c->wave_pending);
		if (c->wave_pending == 4 && !GG_FindSequence(GG_InstModel(oi), anim))
			anim = "std_xdance_n_n_n"; // SPEC-780 16: wave 4 falls back to the dance (neither is in meso_player)
		c->wave_pending = 0;
		c->anim_priority = ANIM_WAVE;
		c->anim_until = level.time + GhoulSeqDuration(ent, anim, kPlayOnce);
		mode = kPlayOnce;
	} else if (!ent->groundentity && PM_OnLadder(ent) && fabsf(ent->velocity[2]) > 2) {
		anim = ent->velocity[2] > 2 ? "std_xladder_up_a_a" : "std_xladder_dwn_a_a";
	} else if (c->lean || c->pm_lean) {
		// SPEC-780 16 lean: `std|cch_hlean{lt|rt}sht_{to|mid|frm}_{a_n|l_2}` — into the lean once, hold, out of it
		// once; lean +1 = left, -1 = right (SPEC-780 12); `l_2` with the rocket launcher (the L class token)
		const char *phase;
		int dir = c->lean ? c->lean : c->pm_lean;
		if (c->lean && c->pm_lean != c->lean) {
			c->pm_lean = c->lean;
			phase = "to";
		} else if (c->lean && level.time < c->pm_lean_until) {
			return; // into the lean plays to its end
		} else if (c->lean) {
			phase = "mid";
		} else if (c->pm_lean_out) {
			if (level.time < c->pm_lean_until)
				return; // out of the lean plays to its end
			c->pm_lean = 0;
			c->pm_lean_out = false;
			phase = NULL;
		} else {
			c->pm_lean_out = true;
			phase = "frm";
		}
		if (phase) {
			int wcl = PM_WeaponClass(c->inv ? InvCurWeapon(c->inv) : 0);
			Com_sprintf(namebuf, sizeof(namebuf), "%s_hlean%ssht_%s_%s", crouched ? "cch" : "std", dir > 0 ? "lt" : "rt",
			            phase, wcl == WC_L ? "l_2" : "a_n");
			anim = namebuf;
			if (strcmp(phase, "mid")) {
				mode = kPlayOnce;
				c->pm_lean_until = level.time + GhoulSeqDuration(ent, anim, kPlayOnce);
			}
		} else
			anim = ent->groundentity ? PM_Locomotion(ent, namebuf, sizeof(namebuf)) : "std_ialert_n_p_n";
	} else if (!ent->groundentity && hspeed > 250) {
		vec3_t f, r, u;
		AngleVectors(c->v_angle, f, r, u);
		anim = DotProduct(f, hv) > 0 ? "std_jrun_fwd_a_a" : "std_ialert_n_p_n";
	} else if (ent->groundentity && crouched != c->pm_crouched) {
		// posture change: going down or standing up once, then the crouched / standing lists (AMEND-005 G18.26;
		// the forward roll into the crouch, `std_xrolltocch_fwd_a_a`, has entity conditions that are UNVERIFIED and
		// is not played)
		c->pm_crouched = crouched;
		anim = PM_LocoName(crouched ? LS_GO_DOWN : LS_STAND_UP, PM_WeaponClass(c->inv ? InvCurWeapon(c->inv) : 0),
		                   "", namebuf, sizeof(namebuf));
		c->pm_posture_until = level.time + GhoulSeqDuration(ent, anim, kPlayOnce);
		mode = kPlayOnce;
	} else if (ent->groundentity && level.time < c->pm_posture_until) {
		return; // the posture change plays to its end
	} else if (ent->groundentity)
		anim = PM_Locomotion(ent, namebuf, sizeof(namebuf));
	else
		anim = c->pm_anim[0] ? c->pm_anim : "std_ialert_n_p_n"; // airborne: keep the current sequence
	if ((c->anim_priority == ANIM_WAVE || c->anim_priority == ANIM_ATTACK) && level.time >= c->anim_until &&
	    anim != c->pm_anim)
		c->anim_priority = ANIM_BASIC;
	const G3Anim *ga = body ? Anim_Human(anim) : NULL;
	// the same name is not replayed — unless the body plays another record (a hit reaction G3 forced)
	if (!strcmp(anim, c->pm_anim) && !(ga && body->cur && body->cur != ga))
		return;
	// played through the player's human body when the sequence is a record of the human animation table: the
	// body applies the SPEC-722 playback rules, among them the hand open/closed parts of the record (G3 report:
	// the original shows `_lopen/_ropen` on death and `_lclosed` with two-handed weapons)
	if (ga) {
		if (!body->SetAnim(ga, mode == kPlayOnce))
			return;
		Com_sprintf(c->pm_anim, sizeof(c->pm_anim), "%s", anim);
		return;
	}
	GhoulID seq = GG_FindSequence(GG_InstModel(oi), anim);
	if (!seq)
		return;
	Com_sprintf(c->pm_anim, sizeof(c->pm_anim), "%s", anim);
	GG_Play(oi, seq, level.time, 0, true, mode);
}
