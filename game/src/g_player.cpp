// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - loading the player module (player.dll) on the server side and the tables the
// game supplies to it: the common import table (19 entries) and the server import table (38 entries)
// with the fire, drop, item-use and fire-time callbacks; per-frame owner context and inventory think.
// Implements: SPEC-001 (#67, #74, #75), SPEC-003 (loading, tables, owner context, context flag),
//             SPEC-602 3/4 (fire times, owner context on the server), SPEC-782 Interface 1

#include "g_local.h"
#include "damage/damage_api.h" /* G3: character hand weapons */
#include "modes/g_modes.h" /* G9: CTF flag queries */

PlayerExport *pe;

static PlayerCommonImport pci;
static PlayerServerImport psi;

static void PM_Com_Printf(const char *fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	gix.dprintf("%s", buf);
}

static void PM_Com_sprintf(char *dst, int size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = _vsnprintf(dst, size - 1, fmt, ap);
	va_end(ap);
	if (n < 0 || n >= size)
		dst[size - 1] = 0;
}

static gclient_t *OwnerClient(edict_t *owner) { return owner ? owner->client : NULL; }

static void PS_FirePrimary(void *, edict_t *owner, PlayerInventory *inv) { WeaponFire(owner, inv, 0); }
static void PS_FireAlt(void *, edict_t *owner, PlayerInventory *inv) { WeaponFire(owner, inv, 1); }
static void PS_ReloadStarted(edict_t *owner) {}
static void PS_DropWeapon(edict_t *owner, int weapon, int rounds) { DropWeaponItem(owner, weapon, rounds); }
static void PS_ItemUsed(edict_t *owner, int itemtype) {}
static void PS_DropItem(edict_t *owner, int itemtype, int count)
{
	if (itemtype == IT_CTFFLAG) {
		GM_DropFlag(owner); /* G9: a dropped CTF flag is the game mode's (SPEC-790) */
		return;
	}
	DropEquipItem(owner, itemtype, count);
}
static void PS_PrecacheAttack(int attack) { WeaponPrecacheAttack(attack); }
static int PS_CarriesFlag(edict_t *owner)
{
	return GM_CarriedFlag(owner) != 0; /* G9: SPEC-003 callback 12 (ERRATA) = owner carries the CTF flag */
}
static float PS_GetFireTimeA(edict_t *owner)
{
	gclient_t *c = OwnerClient(owner);
	return c ? c->firetime : 0;
}
static void PS_ClearFireTimeA(edict_t *owner)
{
	gclient_t *c = OwnerClient(owner);
	if (c)
		c->firetime = 0;
}
static float PS_GetFireTimeB(edict_t *owner)
{
	gclient_t *c = OwnerClient(owner);
	return c ? c->altfiretime : 0;
}
static void PS_ClearFireTimeB(edict_t *owner)
{
	gclient_t *c = OwnerClient(owner);
	if (c)
		c->altfiretime = 0;
}
static void PS_Unused22(edict_t *owner) {}

void PlayerModuleLoad(void)
{
	memset(&pci, 0, sizeof(pci));
	pci.Cvar_Get = gix.cvar;
	pci.Cvar_Set = gix.cvar_set;
	pci.Cvar_SetValue = gix.cvar_setvalue;
	pci.Cmd_Argc = gix.argc;
	pci.Cmd_Argv = gix.argv;
	pci.Cmd_Args = gix.args;
	pci.Com_Printf = PM_Com_Printf;
	pci.Com_sprintf = PM_Com_sprintf;
	pci.Com_DPrintf = PM_Com_Printf;
	pci.FS_LoadFile = gix.FS_LoadFile;
	pci.FS_FreeFile = gix.FS_FreeFile;
	pci.irand = gix.irand;
	pci.ghl_specular = ghl_specular;
	pci.newPlayerModelInfo = NULL;
	pci.LoadGhoulFile = gix.LoadGhoulFile;
	pci.ReadLine = gix.ReadLine;
	pci.PreloadGSQ = gix.PreloadGSQ;
	pci.RegisterGSQSequences = gix.RegisterGSQSequences;
	pci.TurnOffPartsFromGSQ = gix.TurnOffPartsFromGSQ;

	memset(&psi, 0, sizeof(psi));
	psi.GetGhoul = gix.GetGhoul;
	psi.time = &level.time;
	psi.FirePrimary = PS_FirePrimary;
	psi.FireAlt = PS_FireAlt;
	psi.ReloadStarted = PS_ReloadStarted;
	psi.DropWeapon = PS_DropWeapon;
	psi.ItemUsed = PS_ItemUsed;
	psi.DropItem = PS_DropItem;
	psi.PrecacheAttack = PS_PrecacheAttack;
	psi.EffectIndex = gix.effectindex;
	psi.SoundIndex = gix.soundindex;
	psi.StartSound = (void *)gix.sound;
	psi.CarriesFlag = PS_CarriesFlag;
	psi.GetFireTimeA = PS_GetFireTimeA;
	psi.ClearFireTimeA = PS_ClearFireTimeA;
	psi.GetFireTimeB = PS_GetFireTimeB;
	psi.ClearFireTimeB = PS_ClearFireTimeB;
	psi.Cbuf_AddText = gix.AddCommandString;
	psi.Cvar_BitInfo = gix.cvar_info;
	psi.PrintId = gix.SP_Print;
	psi.TextOfId = gix.SP_GetStringText;
	psi.UseFlashpack = WeaponUseFlashpack;
	psi.Unused22 = PS_Unused22;
	psi.UseC4 = WeaponUseC4;
	psi.UseClaymore = WeaponUseClaymore;
	psi.UseMedkit = WeaponUseMedkit;
	psi.UseGrenade = WeaponUseGrenade;
	psi.Goggles = WeaponGoggles;
	psi.SZ_WriteByte = gix.SZ_WriteByte;
	psi.SZ_WriteShort = gix.SZ_WriteShort;
	psi.SZ_WriteLong = gix.SZ_WriteLong;
	psi.ReliableByte = gix.ReliableWriteByteToClient;
	psi.ReliableData = gix.ReliableWriteDataToClient;
	psi.SZ_Init = gix.SZ_Init;
	psi.SZ_Clear = gix.SZ_Clear;
	psi.SZ_Write = gix.SZ_Write;
	psi.WriteChunk = gix.WriteChunk;
	psi.ReadChunk = gix.ReadChunk;

	pe = (PlayerExport *)gix.LoadPlayerDLL(&pci, NULL, &psi, 0);
	if (!pe) {
		gi.error("Game failed to load player DLL");
		return;
	}
	if (pe->version != 1) {
		gi.error("player is version %i, not %i", pe->version, 1);
		return;
	}
	pe->Init();
	if (gix.playercontext)
		*gix.playercontext = (void *)pe->contextflag;
	gi.dprintf("==== InitPlayer Complete ====\n");
}

void PlayerModuleUnload(void)
{
	if (gix.playercontext)
		*gix.playercontext = NULL;
	if (pe) {
		pe->Shutdown();
		gix.UnloadPlayerDLL(0);
		pe = NULL;
	}
}

static const char *weapon_cvar[] = {"nothing", "knife",  "pistol2",    "pistol1", "mpistol", "assault", "sniper",
                                    "slugger", "shotgun", "machinegun", "rocket",  "mpg",     "flamegun"};

static unsigned g_weaponmask;
void PlayerMarkWeapon(int weapon)
{
	if (weapon > 0 && weapon < 32)
		g_weaponmask |= 1u << weapon;
}

// SPEC-722 right-hand weapon: a character's hand weapon "registers the matching player weapon for precaching"
// (attack type → player weapon of the SPEC-722 table; the machine pistol is not registered for `m_x_mmerc`).
// Collected here from the characters spawned by the level (G3 bodies). Oracle: ger1's characters make the
// original register the slug thrower, machine gun, rocket launcher, flame gun and 9 mm view-model sounds and the
// alternate-attack effects of those weapons (capture spsuite_orig_ger1).
static void MarkCharacterWeapons(void)
{
	static const int playerweapon[30] = {0, 1, 1, 3, 2, 4, 5, 6, 6, 7, 7, 8, 9, 9, 10, 0, 11, 11, 12, 12,
	                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 11};
	for (int i = game.maxclients + 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || e->client || !e->g3body)
			continue;
		int fam = e->g3body->family; // human bodies only: vehicles fire their own guns (kos2/sib2 helicopters)
		if (fam != BODY_HUMAN && fam != BODY_MESO && fam != BODY_ECTO && fam != BODY_FEMALE && fam != BODY_MESO_ARMOURED)
			continue;
		int t = e->g3body->RightHandType();
		if (t <= 0 || t >= 30)
			continue;
		if (t == 5 && e->classname && !strcmp(e->classname, "m_x_mmerc"))
			continue;
		PlayerMarkWeapon(playerweapon[t]);
	}
}

// At the level spawn's Ghoul-registry reset: the player module's cached in-view model objects belong to the freed
// registries. Forgetting them here (not at PlayerModuleLevelStart) keeps an in-view precache made while the level
// spawns (game-mode setup) from being registered a second time: the original has one record per in-view model
// (captures rec jpndm2: KNIFE ... THROWHAND once each, records 156-166; OpenMerc had KNIFE, PISTOL1, HURTHAND and
// THROWHAND twice).
void PlayerModuleForgetViewModels(void)
{
	if (pe)
		pe->ForgetViewModels();
}

// SPEC-760 B1.3/B1.4: notify the player module of the level start, then the available weapons
void PlayerModuleLevelStart(void)
{
	if (!pe)
		return;
	MarkCharacterWeapons();
	char *info = gix.cvar_info(0x100 | 0x200 | 0x400);
	if (info) {
		char key[64], value[64];
		const char *s = info;
		while (*s == '\\') {
			s++;
			int n = 0;
			while (*s && *s != '\\' && n < 63)
				key[n++] = *s++;
			key[n] = 0;
			if (*s != '\\')
				break;
			s++;
			n = 0;
			while (*s && *s != '\\' && n < 63)
				value[n++] = *s++;
			value[n] = 0;
			for (int w = 1; w <= 12; w++)
				if (!strcmp(key, weapon_cvar[w]) && atoi(value))
					level.weaponsavailable |= 1 << w;
		}
	}
	unsigned mask = g_weaponmask | (unsigned)level.weaponsavailable;
	if (deathmatch->value)
		mask |= (1u << WP_KNIFE) | (1u << WP_PISTOL1) | (1u << WP_HURTHAND) | (1u << WP_THROWHAND);
	pe->PrecacheViewModels(mask);
	g_weaponmask = 0;
}

// One owner context per client slot (SPEC-003: the inventory keeps the pointer; think and weapon switches read
// the owner entity through it). A brand-new inventory has none, and switching weapons then reads through a NULL
// context inside player.dll, so every inventory created while a client is being put into the server gets a
// neutral context for that client at once (see PlayerSpawning), and PutClientInServer sets it again (SPEC-780
// spawn step 8).
static OwnerContext owner_ctx[MAX_CLIENTS];
static edict_t *spawn_owner;

static OwnerContext *PlayerContextFor(edict_t *ent, PlayerInventory *inv)
{
	gclient_t *c = ent->client;
	OwnerContext *oc = &owner_ctx[(G_EdictNum(ent) - 1) & (MAX_CLIENTS - 1)];
	memset(oc, 0, sizeof(*oc));
	oc->inventory = inv;
	oc->owner = ent;
	oc->kick = c ? c->kick_angles : NULL;
	oc->frametime10 = 1.0f;
	return oc;
}

void PlayerSetIdleContext(edict_t *ent)
{
	if (ent && ent->client && ent->client->inv)
		ent->client->inv->SetOwnerContext(PlayerContextFor(ent, ent->client->inv));
}

void PlayerSpawning(edict_t *ent) { spawn_owner = ent; }

PlayerInventory *PlayerNewInventory(void)
{
	PlayerInventory *inv = pe ? pe->NewInventory() : NULL;
	if (inv && spawn_owner && spawn_owner->client)
		inv->SetOwnerContext(PlayerContextFor(spawn_owner, inv));
	return inv;
}

void PlayerDeleteInventory(PlayerInventory *inv)
{
	if (!pe || !inv)
		return;
	/* Force the shared player-module context to server (0) for the delete. On a
	   listen server the client flag may still be set; Destruct would then call
	   Ghoul Destroy on view instances already freed by GhoulResetLevel. */
	int *ctx = pe->contextflag;
	int saved = ctx ? *ctx : 0;
	if (ctx)
		*ctx = 0;
	pe->DeleteInventory(inv);
	if (ctx)
		*ctx = saved;
}

WeaponDesc *PlayerWeaponDesc(int weapon)
{
	if (!pe || !pe->weapondescs || weapon < 0 || weapon > 14)
		return NULL;
	return (WeaponDesc *)pe->weapondescs[weapon];
}

int InvCurWeapon(PlayerInventory *inv) { return inv ? *(int *)((uint8_t *)inv + INV_CURWEAPON_OFS) : 0; }

ghoul::IGhoulInst *InvViewInst(PlayerInventory *inv)
{
	return inv ? *(ghoul::IGhoulInst **)((uint8_t *)inv + INV_VIEWINST_OFS) : NULL;
}

int InvSpread(PlayerInventory *inv) { return inv ? *(int *)((uint8_t *)inv + INV_SPREAD_OFS) : 0; }

// SPEC-780 2 "Prediction restart"
void PredictionRestart(edict_t *ent)
{
	gclient_t *c = ent->client;
	if (!c || c->pers.isbot)
		return;
	c->resync = (c->resync + 1) & 0xFF;
	gi.WriteByte(svc_restart_predn);
	gi.WriteByte(c->resync);
	gi.unicast(ent, true);
}

// SPEC-780 6 / SPEC-602 4: owner context and the inventory think
void PlayerThinkInventory(edict_t *ent, int attack)
{
	gclient_t *c = ent->client;
	if (!c || !c->inv)
		return;
	OwnerContext *oc = PlayerContextFor(ent, c->inv);
	int b = c->buttons | c->latched_buttons;
	oc->attack = attack;
	oc->altattack = (b & BUTTON_ALTATTACK) != 0;
	oc->extra1 = (b & BUTTON_WEAPON1) != 0;
	oc->extra2 = (b & BUTTON_WEAPON2) != 0;
	oc->leanright = c->lean > 0;
	oc->leanleft = c->lean < 0;
	oc->cinematicfreeze = c->ps.cinematicfreeze;
	oc->remoteview = c->ps.remote_type != 0;
	c->inv->SetOwnerContext(oc);
	c->inv->SetPredicting(c->pers.predicting ? 1 : 0);
	c->inv->Think();
}
