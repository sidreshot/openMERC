// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - the 39 item classnames: table, game-mode filters, Ghoul appearance, pickup
// rules through the player-module inventory, respawn, dropped items, knife pickups, hidden items.
// CTF/Control flag behaviour beyond spawning is left to the game-modes package (G9).
// Implements: SPEC-781 (Behaviour 1-8, 11 partly), SPEC-006 (item classnames)

#include "g_local.h"

int SP_Id(const char *package, const char *entry);
void PlayerMarkWeapon(int weapon);

// SPEC-781 item table
gitem_t itemlist[NUM_ITEMS] = {
	{"item_ammo_knife", IK_AMMO, AM_KNIFE, 1, 30, 1, "KNIFE_PICKUP", "KNIFE_PICKUP", "KNIFE_FULL", "enemy/bolt", "w_knife", NULL, 1.0f, 0, NULL, "Knife"},
	{"item_ammo_pistol", IK_AMMO, AM_9MM, 30, 30, 1, "9MM_AMMO_PICKUP", "9MM_AMMO_PICKUP", "9MM_AMMO_FULL", "Items/Pick-ups/Ammo", "box", "pistolbox", 1.75f, -6, "fade_box", "9mm Ammo"},
	{"item_ammo_shotgun", IK_AMMO, AM_SHELLS, 5, 30, 1, "12_GAUGE_AMMO_PICKUP", "12_GAUGE_AMMO_PICKUP", "12_GAUGE_AMMO_FULL", "Items/Pick-ups/Ammo", "box", "shotgunbox", 1.75f, -6, "fade_box", "12 gauge shells"},
	{"item_ammo_pistol2", IK_AMMO, AM_44, 5, 30, 1, "44_AMMO_PICKUP", "44_AMMO_PICKUP", "44_AMMO_FULL", "Items/Pick-ups/Ammo", "box", "pistol2box", 1.75f, -6, "fade_box", ".44 Ammo"},
	{"item_ammo_auto", IK_AMMO, AM_556, 30, 30, 1, "556_AMMO_PICKUP", "556_AMMO_PICKUP", "556_AMMO_FULL", "Items/Pick-ups/Ammo", "box", "autobox", 1.75f, -6, "fade_box", "5.56 Ammo"},
	{"item_ammo_battery", IK_AMMO, AM_BATTERY, 30, 30, 0, "BATTERY_PICKUP", "BATTERY_PICKUP", "BATTERY_FULL", "Items/Pick-ups/Ammo", "box", "batterybox", 1.75f, -6, "fade_box", "Battery"},
	{"item_ammo_gas", IK_AMMO, AM_FUEL, 30, 30, 0, "GAS_PICKUP", "GAS_PICKUP", "GAS_FULL", "Items/Pick-ups/Ammo", "box", "gasbox", 1.75f, -6, "fade_box", "Flamethrower Gas"},
	{"item_ammo_rocket", IK_AMMO, AM_ROCKETS, 5, 30, 0, "ROCKET_AMMO_PICKUP", "ROCKET_AMMO_PICKUP", "ROCKET_AMMO_FULL", "Items/Pick-ups/Ammo", "box", "rocketbox", 1.75f, -6, "fade_box", "NM-22 Rockets"},
	{"item_ammo_slug", IK_AMMO, AM_SLUG, 10, 30, 0, "SLUGS_PICKUP", "SLUGS_PICKUP", "SLUGS_FULL", "Items/Pick-ups/Ammo", "box", "slugbox", 1.75f, -6, "fade_box", "Slugs"},
	{"item_ammo_sp_pistol", IK_AMMO, AM_9MM, 30, 30, 1, "9MM_AMMO_PICKUP", "9MM_AMMO_PICKUP", "9MM_AMMO_FULL", "Items/Pick-ups/Ammo_single", "ammo_9mm", "ammo_9mm_single", 1.75f, 0, NULL, "9mm Ammo"},
	{"item_ammo_sp_shotgun", IK_AMMO, AM_SHELLS, 5, 30, 1, "12_GAUGE_AMMO_PICKUP", "12_GAUGE_AMMO_PICKUP", "12_GAUGE_AMMO_FULL", "Items/Pick-ups/Ammo_single", "ammo_shotgun", "ammo_00_single", 1.75f, 1, NULL, "12 gauge shells"},
	{"item_ammo_sp_pistol2", IK_AMMO, AM_44, 5, 30, 1, "44_AMMO_PICKUP", "44_AMMO_PICKUP", "44_AMMO_FULL", "Items/Pick-ups/Ammo_single", "ammo_44", "ammo_44_single", 2.0f, 0, NULL, ".44 Ammo"},
	{"item_ammo_sp_auto", IK_AMMO, AM_556, 30, 30, 1, "556_AMMO_PICKUP", "556_AMMO_PICKUP", "556_AMMO_FULL", "Items/Pick-ups/Ammo_single", "ammo_556", "ammo_556_single", 2.0f, 0, "fade_ammo_556", "5.56 Ammo"},
	{"item_ammo_sp_battery", IK_AMMO, AM_BATTERY, 30, 30, 0, "BATTERY_PICKUP", "BATTERY_PICKUP", "BATTERY_FULL", "Items/Pick-ups/Ammo_single", "ammo_battery", "ammo_bat", 2.0f, 0, NULL, "Battery"},
	{"item_ammo_sp_gas", IK_AMMO, AM_FUEL, 30, 30, 0, "GAS_PICKUP", "GAS_PICKUP", "GAS_FULL", "Items/Pick-ups/Ammo_single", "ammo_gas", "ammo_gas_single", 2.0f, 0, NULL, "Flamethrower Gas"},
	{"item_ammo_sp_rocket", IK_AMMO, AM_ROCKETS, 5, 30, 0, "ROCKET_AMMO_PICKUP", "ROCKET_AMMO_PICKUP", "ROCKET_AMMO_FULL", "Items/Pick-ups/Ammo_single", "ammo_rocket", "ammo_rocket_single", 2.0f, -0.5f, "fade_ammo_rocket", "NM-22 Rockets"},
	{"item_ammo_sp_slug", IK_AMMO, AM_SLUG, 10, 30, 0, "SLUGS_PICKUP", "SLUGS_PICKUP", "SLUGS_FULL", "Items/Pick-ups/Ammo_single", "ammo_shotgun", "ammo_slug_single", 2.0f, 1, NULL, "Slugs"},
	{"item_weapon_pistol2", IK_WEAPON, WP_PISTOL2, 0, 30, 1, "44_PISTOL_PICKUP", "44_AMMO_PICKUP", "44_AMMO_FULL", "enemy/bolt", "w_pistol2", NULL, 1.75f, -1.5f, "fade_pistol2", ".44 Pistol"},
	{"item_weapon_pistol1", IK_WEAPON, WP_PISTOL1, 0, 30, 1, "9MM_PISTOL_PICKUP", "9MM_AMMO_PICKUP", "9MM_AMMO_FULL", "enemy/bolt", "w_pistol1", NULL, 1.75f, -1.5f, "fade_pistol1", "9mm Pistol"},
	{"item_weapon_machinepistol", IK_WEAPON, WP_MPISTOL, 0, 30, 1, "9MM_SMG_PICKUP", "9MM_AMMO_PICKUP", "9MM_AMMO_FULL", "enemy/bolt", "w_machinepistol", NULL, 1.75f, -3, "fade_machinepistol", "9mm Suppressed SMG"},
	{"item_weapon_assault_rifle", IK_WEAPON, WP_ASSAULT, 0, 30, 1, "556_SMG_PICKUP", "556_AMMO_PICKUP", "556_AMMO_FULL", "enemy/bolt", "w_assault_rifle", NULL, 1.75f, -2, "fade_assault_rifle", "5.56 SMG"},
	{"item_weapon_sniper_rifle", IK_WEAPON, WP_SNIPER, 0, 30, 1, "556_SNIPER_PICKUP", "556_AMMO_PICKUP", "556_AMMO_FULL", "enemy/bolt", "w_sniperrifle", NULL, 1.75f, -2, "fade_sniperrifle", "5.56 Sniper Rifle"},
	{"item_weapon_autoshotgun", IK_WEAPON, WP_SLUGGER, 0, 60, 0, "SLUGTHROWER_PICKUP", "SLUGS_PICKUP", "SLUGS_FULL", "enemy/bolt", "w_autoshotgun", NULL, 1.75f, -8.5f, "fade_autoshotgun", "Repeating Slug Thrower"},
	{"item_weapon_shotgun", IK_WEAPON, WP_SHOTGUN, 0, 30, 1, "12_GAUGE_SHOTGUN_PICKUP", "12_GAUGE_AMMO_PICKUP", "12_GAUGE_AMMO_FULL", "enemy/bolt", "w_shotgun", NULL, 1.75f, -3, "fade_shotgun", "12 Gauge Shotgun"},
	{"item_weapon_machinegun", IK_WEAPON, WP_MACHINEGUN, 0, 30, 1, "HEAVY_MACHINEGUN_PICKUP", "556_AMMO_PICKUP", "556_AMMO_FULL", "enemy/bolt", "w_machinegun", NULL, 1.75f, -3, "fade_machinegun", "Heavy Machinegun"},
	{"item_weapon_rocketlauncher", IK_WEAPON, WP_ROCKET, 0, 75, 0, "ROCKET_LAUNCHER_PICKUP", "ROCKET_AMMO_PICKUP", "ROCKET_AMMO_FULL", "enemy/bolt", "w_rocket", NULL, 1.75f, -10.5f, "fade_rocket", "M-1943 Rocket Launcher"},
	{"item_weapon_microwavepulse", IK_WEAPON, WP_MPG, 0, 90, 0, "MICROWAVE_PULSE_PICKUP", "BATTERY_PICKUP", "BATTERY_FULL", "enemy/bolt", "w_mpg", NULL, 1.75f, -8, "fade_mpg", "Microwave Pulse"},
	{"item_weapon_flamethrower", IK_WEAPON, WP_FLAMEGUN, 0, 60, 0, "FLAME_GUN_PICKUP", "GAS_PICKUP", "GAS_FULL", "enemy/bolt", "w_flamethrower", NULL, 1.75f, -2.5f, "fade_flamethrower", "L-32 Flame Gun"},
	{"item_equip_armor", IK_ARMOR, 0, 50, 30, 1, "ARMOR_PICKUP", "ARMOR_PICKUP", "ARMOR_ALREADY_FULL", "Items/Pick-ups/Armor", "armor", NULL, 1.25f, 0, "armorfade", "Armor"},
	{"item_equip_flashpack", IK_EQUIP, IT_FLASHPACK, 1, 30, 1, "FLASHPACK_PICKUP", "FLASHPACK_PICKUP", "FLASHPACK_FULL", "Items/Projectiles", "flashpak", NULL, 1.75f, -9, "fade_flashpak", "Flashpack"},
	{"item_equip_c4", IK_EQUIP, IT_C4, 1, 30, 1, "C4_PICKUP", "C4_PICKUP", "C4_FULL", "Items/Projectiles", "c4", NULL, 1.75f, -5, "fade_c4", "C4"},
	{"item_equip_light_goggles", IK_EQUIP, IT_GOGGLES, 100, 30, 1, "LIGHTGOGGLES_PICKUP", "LIGHTGOGGLES_PICKUP", "LIGHTGOGGLES_FULL", "enemy/bolt", "acc_nightvision", NULL, 2.0f, -4.5f, "fade_nightvision", "Light Amplification Goggles"},
	{"item_equip_claymore", IK_EQUIP, IT_CLAYMORE, 1, 30, 1, "CLAYMORE_PICKUP", "CLAYMORE_PICKUP", "CLAYMORE_FULL", "Items/Projectiles", "claymore", NULL, 1.75f, -5, "fade_claymore", "Mine"},
	{"item_equip_medkit", IK_EQUIP, IT_MEDKIT, 1, 30, 1, "MEDKIT_PICKUP", "MEDKIT_PICKUP", "MEDKIT_FULL", "Items/Pick-ups/MedKit", "medkit", NULL, 1.75f, 0.5f, "fade_medkit", "MedKit"},
	{"item_equip_grenade", IK_EQUIP, IT_GRENADE, 1, 30, 1, "GRENADE_PICKUP", "GRENADE_PICKUP", "GRENADE_FULL", "Items/Projectiles", "grenade", NULL, 1.75f, 0, "fade_grenade", "Grenade"},
	{"item_ctf_flag", IK_EQUIP, IT_CTFFLAG, 1, 0, 1, "CTF_FLAG_PICKUP", "CTF_FLAG_PICKUP_2", "CTF_FLAG_PICKUP", "Items/ctf_flag", "flag_ground_idle", "ctf_flag_red", 1.75f, 0, NULL, "Flag"},
	{"item_health_small", IK_HEALTH, 0, 25, 30, 1, "HEALTH_PICKUP", "HEALTH_PICKUP", "HEALTH_PICKUP", "Items/Pick-ups/healthsm", "health", NULL, 1.75f, 0, "fade_health", "Small Health"},
	{"item_health_large", IK_HEALTH, 0, 50, 30, 1, "HEALTH_PICKUP", "HEALTH_PICKUP", "HEALTH_PICKUP", "Items/Pick-ups/healthbig", "health", NULL, 1.75f, 0, "fade_health", "Large Health"},
	{"item_control_flag", IK_EQUIP, 0, 0, 0, 1, NULL, NULL, NULL, "Items/ctf_flag", "flag_ground_idle", "ctf_flag_grey", 1.75f, 0, NULL, "Control Flag"},
};

#define ITEMNUM(it) ITEM_INDEX(it)
static const float ammo_mult[5] = {2.0f, 1.0f, 1.0f, 1.0f, 5.0f};
static const float armor_mult[5] = {2.0f, 1.0f, 1.0f, 1.0f, 0.5f};
static const int weight_limit[5] = {0, 10, 7, 5, 5};

void InitItems(void) {}

gitem_t *GetItemByIndex(int index)
{
	if (index < 1 || index > NUM_ITEMS)
		return NULL;
	return &itemlist[index - 1];
}

gitem_t *FindItemByClassname(const char *classname)
{
	for (int i = 0; i < NUM_ITEMS; i++)
		if (!strcmp(itemlist[i].classname, classname))
			return &itemlist[i];
	return NULL;
}

gitem_t *FindItemForWeapon(int weapon)
{
	for (int i = 0; i < NUM_ITEMS; i++)
		if (itemlist[i].kind == IK_WEAPON && itemlist[i].value == weapon)
			return &itemlist[i];
	return NULL;
}

gitem_t *FindItemForItemType(int itemtype)
{
	for (int i = 0; i < NUM_ITEMS; i++)
		if (itemlist[i].kind == IK_EQUIP && itemlist[i].value == itemtype && ITEMNUM(&itemlist[i]) != 39)
			return &itemlist[i];
	return NULL;
}

static int Mode(void) { return (int)deathmatch->value; }
static bool DMFamily(void) { return Mode() != 0; }
static int DF(void) { return (int)dmflags->value; }
static int clamp4(float v)
{
	int i = (int)v;
	return i < 0 ? 0 : i > 4 ? 4 : i;
}

static void ItemMessage(edict_t *to, const char *entry)
{
	if (!entry || !to || !to->client)
		return;
	int id = SP_Id("items", entry);
	if (id >= 0)
		gix.SP_Print(to, id);
}

//======================================================================
// SPEC-781 Behaviour 1: game-mode filter

static int g_numcontrol;

qboolean SpawnItemCheck(edict_t *ent, gitem_t *item)
{
	int idx = ITEMNUM(item);
	int dm = Mode(), df = DF();
	if (idx == 36 && dm != 4) {
		gi.dprintf("No flags in non control or CTF play\n");
		G_FreeEdict(ent);
		return false;
	}
	if (idx == 39) {
		if (dm != 6) {
			gi.dprintf("No flags in non control or CTF play\n");
			G_FreeEdict(ent);
			return false;
		}
		if (g_numcontrol >= 5) {
			gi.dprintf("Max Control Flag count of 5 reached. Dumping flag.\n");
			G_FreeEdict(ent);
			return false;
		}
	}
	if (dm == 0)
		return true;
	bool realistic = dm == 5, arsenal = dm == 3, ctf = dm == 4, control = dm == 6;
	bool remove = false;
	if ((df & 0x1) && item->kind == IK_HEALTH && !realistic)
		remove = true;
	if ((df & 0x2) && item->kind == IK_EQUIP && !realistic && !(ctf && idx == 36) && !(control && idx == 39))
		remove = true;
	if ((df & 0x400) && item->kind == IK_ARMOR)
		remove = true;
	if (item->kind == IK_AMMO && (arsenal || ((df & 0x800) && !realistic)))
		remove = true;
	if (item->kind == IK_WEAPON && arsenal)
		remove = true;
	if ((df & 0x2000) && !arsenal && (item->kind == IK_WEAPON || item->kind == IK_AMMO) && !item->standard)
		remove = true;
	if (realistic && idx == 37)
		remove = true;
	if (remove) {
		// SPEC-760 B1.1: the entity is left as an inert record
		ent->solid = SOLID_NOT;
		ent->svflags |= SVF_NOCLIENT;
		return false;
	}
	return true;
}

//======================================================================

static void Touch_Item(edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf);

static void ItemSetBox(edict_t *ent, gitem_t *it, bool hasmodel)
{
	int idx = ITEMNUM(it);
	float s = it->scale;
	if (!hasmodel) {
		VectorSet(ent->mins, -8, -8, -8);
		VectorSet(ent->maxs, 8, 8, 8);
	} else if (idx == 36 || idx == 39) {
		VectorSet(ent->mins, -20 * s, -20 * s, 0);
		VectorSet(ent->maxs, 20 * s, 20 * s, 30 * s);
	} else {
		VectorSet(ent->mins, -8 * s, -8 * s, it->boxbottom);
		VectorSet(ent->maxs, 8 * s, 8 * s, 8 * s);
	}
}

static bool ItemSetModel(edict_t *ent, gitem_t *it)
{
	int idx = ITEMNUM(it);
	const char *seq = NULL;
	if (idx == 29 && DMFamily())
		seq = "armordm";
	else if (idx == 31)
		seq = "c4";
	else if (idx == 36 || idx == 39)
		seq = "quake_ground";
	const char *skin = it->skin;
	if (idx == 36)
		skin = ent->itemcount == 1 ? "ctf_flag_blue" : "ctf_flag_red";
	// pickups whose kind has a skin (ammo boxes, single ammunition): a record with only that skin, keyed by it
	// (G9's SP captures: `ammo_shotgun` with AMMO_00_SINGLE only; rec jpndm2: after the level-start BOX record with
	// all skins (G18.28), the map's 5.56/9 mm/shotgun/.44 boxes add BOX records with AUTOBOX, PISTOLBOX, SHOTGUNBOX,
	// PISTOL2BOX only, records 139-142, in map order); the other items use the all-skins record
	bool single = it->skin && idx != 36;
	ghoul::IGhoulInst *inst = single ? GhoulSetModelSkinned(ent, it->dir, it->model, skin, it->scale, seq, 0)
	                                 : GhoulSetModel(ent, it->dir, it->model, skin, it->scale, seq, 0);
	if (!inst) {
		gi.dprintf("No model specified for %s, can't spawn\n", it->classname);
		return false;
	}
	return true;
}

static edict_t *g_itemlist[MAX_ITEM_ENTS];
static int g_numitems;

void ItemRemoveFromList(edict_t *ent)
{
	for (int i = 0; i < g_numitems; i++)
		if (g_itemlist[i] == ent) {
			g_itemlist[i] = g_itemlist[--g_numitems];
			break;
		}
	ent->flags &= ~FL_ITEMLIST;
}

static void ItemAddToList(edict_t *ent)
{
	if (g_numitems < MAX_ITEM_ENTS) {
		g_itemlist[g_numitems++] = ent;
		ent->flags |= FL_ITEMLIST;
	}
}

// item-list accessors for the savegame (G10, SPEC-781 "Savegame chunks" PLNM/PLPI): the list in its order, cleared
// and refilled when a level is read
extern "C" int ItemList_Count(void) { return g_numitems; }
extern "C" edict_t *ItemList_Get(int i) { return i >= 0 && i < g_numitems ? g_itemlist[i] : NULL; }
extern "C" void ItemList_Clear(void) { g_numitems = 0; }
extern "C" void ItemList_Add(edict_t *ent)
{
	if (ent)
		ItemAddToList(ent);
}

void SpawnItem(edict_t *ent, gitem_t *item)
{
	int idx = ITEMNUM(item);
	if (Mode() == 5 && idx == 38) // Realistic: large health holds a medkit
		item = &itemlist[34 - 1];
	idx = ITEMNUM(item);

	ent->item = item;
	ent->classname = (char *)item->classname;
	ent->spawnflags |= ITEM_TRIGGER_SPAWN;

	if (ent->count > 0)
		ent->itemcount = ent->count;
	else if (ent->count == 0)
		ent->itemcount = item->defcount;
	else
		ent->itemcount = gix.irand(0, -ent->count);
	if (item->kind == IK_ARMOR && ent->itemcount > 100)
		ent->itemcount = 100;
	if (idx == 1)
		ent->itemcount = 1;

	bool hasmodel = ItemSetModel(ent, item);
	ItemSetBox(ent, item, hasmodel);

	ent->solid = SOLID_TRIGGER;
	ent->movetype = (ent->spawnflags & ITEM_NO_FALL) ? MOVETYPE_NONE : MOVETYPE_TOSS;
	ent->touch = Touch_Item;
	ent->clipmask = MASK_SOLID;

	if (item->kind == IK_WEAPON)
		PlayerMarkWeapon(item->value);

	if (DMFamily() && (DF() & 0x1000) && Mode() != 3 && (item->kind == IK_WEAPON || item->kind == IK_ARMOR)) {
		ent->movetype = MOVETYPE_NONE;
		ent->gravity = 0;
		ent->s.origin[2] += 6;
		ent->s.effects |= EF_AUTOROTATE;
		if (item->kind == IK_WEAPON)
			ent->s.angles[ROLL] = item->value == WP_MACHINEGUN ? -90 : 90;
	}
	if (idx == 39)
		g_numcontrol++;
	// SPEC-781 spawnflag 128: ammunition that respawns even in single player precaches the respawn effect and sound
	// (spsuite ger4, jpn3: `environ/irespawn`, `dm/irespawn.wav` only in the oracle before this)
	if (item->kind == IK_AMMO && (ent->spawnflags & ITEM_SP_RESPAWN)) {
		gi.effectindex("environ/irespawn");
		gi.soundindex("dm/irespawn.wav");
	}

	VectorCopy(ent->s.origin, ent->item_origin);
	VectorCopy(ent->s.angles, ent->item_angles);
	ItemAddToList(ent);
	gi.linkentity(ent);
}

void ItemsLevelStart(void)
{
	g_numitems = 0;
	g_numcontrol = 0;
	// AMEND-005 G18.28: at every level start, in all modes, before the entities, the item kinds with a model list
	// register their Ghoul model record whether or not such an item is on the map, in item-number order. The weapon
	// kinds (18-28) and the goggles (32) name lists the stock data does not have (`enemy/bolt/pistol2` ...
	// `flamethrower`, `nightvision`) load nothing, but their records are kept: the original has 12 empty records
	// between AMMO_ROCKET and MEDKIT (captures suite1/rec jpndm2, records 124-135; the UNVERIFIED part of G18.28).
	static const char *const blank[] = {"pistol2", "pistol1", "machinepistol", "assault_rifle", "sniperrifle",
		"autoshotgun", "shotgun", "machinegun", "rocket", "mpg", "flamethrower"};
	static const int kinds[] = {2, 3, 4, 5, 6, 7, 8, 9, 13, 16, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
		31, 32, 33, 34, 35, 37, 38};
	for (int n : kinds) {
		gitem_t *it = &itemlist[n - 1];
		if (n >= 18 && n <= 28)
			GhoulModelObject("enemy/bolt", blank[n - 18], NULL, true);
		else if (n == 32)
			GhoulModelObject("enemy/bolt", "nightvision", NULL, true);
		else if (it->skin && !Q_stricmp(it->dir, "Items/Pick-ups/Ammo_single")) {
			// the single 5.56 / rocket rounds: their one-skin record (as the pickups spawn it)
			GhoulModelObjectSkinned(it->dir, it->model, it->skin);
		} else
			GhoulModelObject(it->dir, it->model, NULL, true);
	}
	if (deathmatch->value) {
		gi.effectindex("environ/irespawn");
		gi.effectindex("environ/prespawn");
	}
}

// after all entities spawned: hide spawnflag-8 items (SPEC-781 1.6)
void ItemsFinalize(void)
{
	for (int i = 0; i < g_numitems; i++) {
		edict_t *e = g_itemlist[i];
		if (e->spawnflags & ITEM_HIDDEN) {
			e->svflags |= SVF_NOCLIENT;
			e->solid = SOLID_NOT;
			gi.linkentity(e);
		}
	}
}

void RevealHiddenItems(const char *targetname)
{
	for (int i = 0; i < g_numitems; i++) {
		edict_t *e = g_itemlist[i];
		if ((e->spawnflags & ITEM_HIDDEN) && e->targetname && !strcmp(e->targetname, targetname)) {
			e->spawnflags &= ~ITEM_HIDDEN;
			e->svflags &= ~SVF_NOCLIENT;
			e->solid = SOLID_TRIGGER;
			gi.linkentity(e);
		}
	}
}

void ItemsFrame(void) {}

//======================================================================
// respawn (SPEC-781 Behaviour 6)

static void ItemRespawnThink(edict_t *ent)
{
	if (ent->itemstate == 1 && level.time >= ent->dietime - 0.001f) {
		G_AddEvent(ent, 22, 0);
		FX_ClearSlots(ent);
		GhoulSetTint(ent, 1, 1, 1, 1);
		ent->itemstate = 2;
	}
	if (ent->solid == SOLID_NOT && level.time >= ent->fuse - 0.001f) {
		ent->solid = SOLID_TRIGGER;
		if (ent->item->fadeseq)
			GhoulPlay(ent, ent->item->model, 0);
		gi.linkentity(ent);
	}
	if (ent->itemstate != 2 || ent->solid == SOLID_NOT)
		ent->nextthink = level.time + FRAMETIME;
}

static void DoRespawn(edict_t *ent)
{
	ent->svflags &= ~SVF_NOCLIENT;
	ent->flags &= ~FL_RESPAWN;
	FX_Effect(gi.effectindex("environ/irespawn"), ent->s.origin, MULTICAST_PVS);
	gi.sound(ent, CHAN_ITEM, gi.soundindex("dm/irespawn.wav"), 0.6f, ATTN_NORM, 0);
	float fade = 0;
	if (ent->item->fadeseq) {
		GhoulPlay(ent, ent->item->fadeseq, 1);
		fade = GhoulSeqDuration(ent, ent->item->fadeseq, 1);
	}
	ent->fuse = level.time + fade;
	ent->dietime = level.time + 0.8f;
	ent->itemstate = 1;
	ent->solid = fade > 0 ? SOLID_NOT : SOLID_TRIGGER;
	gi.linkentity(ent);
	ent->think = ItemRespawnThink;
	ent->nextthink = level.time + FRAMETIME;
}

static int PlayerSlotsInUse(void)
{
	int n = 0;
	for (int i = 0; i < game.maxclients; i++)
		if (game.clients[i].pers.connected)
			n++;
	return n;
}

static void SetRespawn(edict_t *ent, float delay)
{
	ent->flags |= FL_RESPAWN;
	ent->svflags |= SVF_NOCLIENT;
	ent->solid = SOLID_NOT;
	ent->nextthink = level.time + delay;
	ent->think = DoRespawn;
	gi.linkentity(ent);
}

static void ItemConsumed(edict_t *ent, bool respawn)
{
	if (respawn) {
		float T = ent->item->respawn;
		float delay = T;
		if (DMFamily()) {
			int N = PlayerSlotsInUse();
			delay = T * sqrtf(2.0f / (N > 2 ? N : 2));
		}
		SetRespawn(ent, delay);
	} else {
		ent->touch = NULL;
		ent->nextthink = level.time + FRAMETIME;
		ent->think = G_FreeEdict;
		ent->svflags |= SVF_NOCLIENT;
		ent->solid = SOLID_NOT;
		gi.linkentity(ent);
	}
}

//======================================================================
// pickup (SPEC-781 Behaviour 2-5)

static void Refuse(edict_t *ent, edict_t *other, const char *msg)
{
	if (level.time < ent->refuse_time)
		return;
	ent->refuse_time = level.time + 5;
	ItemMessage(other, msg);
}

static void PickupSound(edict_t *other) { G_Sound(other, CHAN_ITEM, "weapons/gpistol/cock.wav", 1, ATTN_IDLE); }

static int CarryLimit(int itemtype)
{
	bool sp = !DMFamily();
	switch (itemtype) {
	case IT_FLASHPACK: return sp ? 6 : -1;
	case IT_C4: return sp ? 4 : -1;
	case IT_GOGGLES: return 100;
	case IT_MEDKIT:
		if (Mode() == 5)
			return 1;
		return (!sp && (DF() & 0x10)) ? 1 : 2;
	case IT_GRENADE: return sp ? 6 : -1;
	}
	return -1;
}

static void Touch_Item(edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	gitem_t *it = ent->item;
	if (!it || !other->client)
		return;
	int idx = ITEMNUM(it);
	if (idx != 39 && other->health <= 0)
		return;
	gclient_t *cl = other->client;
	PlayerInventory *inv = cl->inv;
	if (!inv)
		return;

	cl->spawnprotect_time = 0; // a touch ends spawn protection (SPEC-780 4)

	bool dropped = (ent->spawnflags & DROPPED_ITEM) != 0;
	if (dropped && ent->owner == other && level.time < ent->timestamp + 2)
		return;

	bool sp = !DMFamily();
	bool realistic = Mode() == 5;
	bool weaponsstay = (DF() & 0x4) && (Mode() == 1 || Mode() == 2 || Mode() == 4 || Mode() == 6 || Mode() == 7);

	switch (it->kind) {
	case IK_AMMO: {
		int amount = idx == 1 ? 1 : (int)(ent->itemcount * ammo_mult[clamp4(gix.cvar_value("sk_spawning"))]);
		if (!inv->AddAmmo(it->value, amount)) {
			Refuse(ent, other, it->msg_refuse);
			return;
		}
		PickupSound(other);
		ItemMessage(other, it->msg_pickup);
		bool respawn = (!sp && !dropped) || (sp && (ent->spawnflags & ITEM_SP_RESPAWN));
		ItemConsumed(ent, respawn);
		return;
	}
	case IK_WEAPON: {
		int w = it->value;
		int rounds = dropped ? ent->itemcount : -1;
		int L = sp ? weight_limit[clamp4(gix.cvar_value("sk_maxencum"))] : realistic ? 4 : 0;
		if (!inv->OwnsWeapon(w)) {
			bool encumbered = L > 0 && inv->WeaponWeight(w) + inv->TotalWeight() > L;
			if (!encumbered) {
				inv->GiveWeapon(w, rounds);
				if (cl->pers.bestweap[0] && !realistic && inv->BestWeapon() == w)
					inv->RequestBest();
				PickupSound(other);
				ItemMessage(other, it->msg_pickup);
				if (weaponsstay && !dropped)
					return;
				ItemConsumed(ent, !sp && !dropped && !realistic);
				return;
			}
			if (level.time < ent->refuse_time)
				return;
			if (sp && !(ent->spawnflags & ITEM_AMMO_TAKEN)) {
				if (inv->AddAmmoForWeapon(w, dropped ? ent->itemcount : 0)) {
					PickupSound(other);
					ItemMessage(other, it->msg_secondary);
					ent->spawnflags |= ITEM_AMMO_TAKEN;
				}
			}
			ent->refuse_time = level.time + 5;
			ItemMessage(other, "ENCUMBERED");
			return;
		}
		if (weaponsstay && !dropped)
			return;
		if (realistic) {
			Refuse(ent, other, "ALREADY_HAVE_THIS");
			return;
		}
		if (!inv->AddAmmoForWeapon(w, dropped ? ent->itemcount : 0)) {
			Refuse(ent, other, it->msg_refuse);
			return;
		}
		PickupSound(other);
		ItemMessage(other, it->msg_secondary);
		ItemConsumed(ent, !sp && !dropped);
		return;
	}
	case IK_ARMOR: {
		int before = inv->Armor();
		int amount = (int)(ent->itemcount * armor_mult[clamp4(gix.cvar_value("sk_toughness"))]);
		inv->AddArmor(amount);
		if (inv->Armor() == before) {
			Refuse(ent, other, it->msg_refuse);
			return;
		}
		G_Sound(other, CHAN_ITEM, "dm/armorpu.wav", 1, ATTN_IDLE);
		PModel_ArmourChanged(other, inv->Armor() > 0); // armoured model (SPEC-780 16)
		ItemMessage(other, it->msg_pickup);
		ItemConsumed(ent, !sp && !dropped);
		return;
	}
	case IK_EQUIP: {
		if (idx == 36 || idx == 39)
			return; // flag rules: game modes (G9)
		if (!inv->AddItems(it->value, ent->itemcount, CarryLimit(it->value))) {
			Refuse(ent, other, it->msg_refuse);
			return;
		}
		PickupSound(other);
		ItemMessage(other, it->msg_pickup);
		ItemConsumed(ent, !sp && !dropped);
		return;
	}
	case IK_HEALTH: {
		if (other->health >= other->max_health)
			return;
		other->health += ent->itemcount;
		if (other->health > other->max_health)
			other->health = other->max_health;
		G_Sound(other, CHAN_ITEM, "dm/healthpu.wav", 1, ATTN_IDLE);
		G_Sound(other, CHAN_ITEM, "dm/healthpu.wav", 1, ATTN_IDLE);
		ItemMessage(other, it->msg_pickup);
		ItemConsumed(ent, !sp && !dropped);
		return;
	}
	}
}

//======================================================================
// dropped items (SPEC-781 Behaviour 7)

static edict_t *DropCommon(edict_t *ent, gitem_t *it, int amount, bool weapon)
{
	int mode = Mode();
	if (mode == 3)
		return NULL;
	if (mode != 0 && !ent->ghoulInst)
		return NULL;

	edict_t *drop = G_Spawn();
	drop->classname = (char *)it->classname;
	drop->item = it;
	drop->spawnflags = DROPPED_ITEM | ITEM_TRIGGER_SPAWN;
	drop->owner = ent;
	drop->timestamp = level.time;
	drop->itemcount = amount;

	vec3_t fwd, flat;
	AngleVectors(ent->client ? ent->client->v_angle : ent->s.angles, fwd, NULL, NULL);
	VectorSet(flat, fwd[0], fwd[1], 0);
	VectorNormalize(flat);

	vec3_t start;
	bool fromBolt = false;
	if (mode != 0)
		fromBolt = GhoulBoltPos(ent, weapon ? "wbolt_hand_r" : "wbolt_hand_l", start);
	if (!fromBolt) {
		VectorCopy(ent->s.origin, start);
		if (mode == 4 && !weapon && !ent->ghoulInst)
			start[2] += 40;
	}
	vec3_t end;
	VectorMA(start, 20, flat, end);
	if (weapon) {
		trace_t tr = gi.trace(start, NULL, NULL, end, ent, MASK_SOLID);
		VectorCopy(tr.endpos, drop->s.origin);
	} else
		VectorCopy(end, drop->s.origin);

	bool hasmodel = ItemSetModel(drop, it);
	ItemSetBox(drop, it, hasmodel);
	drop->solid = SOLID_TRIGGER;
	drop->movetype = MOVETYPE_TOSS;
	drop->touch = Touch_Item;
	drop->clipmask = MASK_SOLID;
	VectorScale(flat, 250, drop->velocity);
	drop->velocity[2] = 150;
	if (mode != 0) {
		drop->think = G_FreeEdict;
		drop->nextthink = level.time + 20;
	}
	ItemAddToList(drop);
	gi.linkentity(drop);
	return drop;
}

edict_t *DropWeaponItem(edict_t *ent, int weapon, int rounds)
{
	gitem_t *it = FindItemForWeapon(weapon);
	if (!it)
		return NULL;
	edict_t *d = DropCommon(ent, it, rounds, true);
	if (d && ent->client && (Mode() == 0 || ent->health > 0)) {
		static const char *dn[] = {NULL,          NULL,          "DROPPED_44PISTOL", "DROPPED_9MMPISTOL",
		                           "DROPPED_9MMSUPSMG", "DROPPED_556SMG", "DROPPED_SNIPER", "DROPPED_SLUGTHROWER",
		                           "DROPPED_SHOTGUN",   "DROPPED_HVYMACH", "DROPPED_ROCKETLAUNCHER", "DROPPED_MPG",
		                           "DROPPED_FLAMEGUN"};
		int textid = SP_Id("dm_generic", "TEXT_DROPPED_WEAPON");
		int nameid = (weapon >= 2 && weapon <= 12) ? SP_Id("items", dn[weapon]) : -1;
		if (textid >= 0 && nameid >= 0)
			gix.SP_Print(ent, textid, nameid);
	}
	return d;
}

edict_t *DropEquipItem(edict_t *ent, int itemtype, int count)
{
	gitem_t *it = FindItemForItemType(itemtype);
	if (!it)
		return NULL;
	return DropCommon(ent, it, count, false);
}

edict_t *SpawnKnifePickup(const vec3_t origin, const vec3_t angles, float life)
{
	edict_t *k = G_Spawn();
	gitem_t *it = &itemlist[0];
	VectorCopy(origin, k->s.origin);
	VectorCopy(angles, k->s.angles);
	k->classname = (char *)it->classname;
	k->item = it;
	k->itemcount = 1;
	k->spawnflags = DROPPED_ITEM | ITEM_TRIGGER_SPAWN;
	bool hasmodel = ItemSetModel(k, it);
	ItemSetBox(k, it, hasmodel);
	k->solid = SOLID_TRIGGER;
	k->movetype = MOVETYPE_NONE;
	k->touch = Touch_Item;
	k->think = G_FreeEdict;
	k->nextthink = level.time + life;
	ItemAddToList(k);
	gi.linkentity(k);
	return k;
}

void PrecacheItem(gitem_t *it) {}

void Cmd_ListPickups_f(edict_t *ent)
{
	gi.cprintf(ent, PRINT_HIGH, "//\n// begin pickup list -- map:%s\n//\n", level.mapname);
	gi.cprintf(ent, PRINT_HIGH, "#     classname                 index  count  location\n");
	for (int i = 0; i < g_numitems; i++) {
		edict_t *e = g_itemlist[i];
		gi.cprintf(ent, PRINT_HIGH, "%-5d %-26s %-6d %-6d %s\n", i, e->classname, ITEMNUM(e->item), e->itemcount,
		           vtos(e->s.origin));
	}
	gi.cprintf(ent, PRINT_HIGH, "// end pickup list\n");
}
