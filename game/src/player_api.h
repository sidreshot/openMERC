// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - the game side of the player-module (player.dll) boundary: the common and
// server import tables the game builds, the export table, the owner context, the item slot and the
// inventory object interface (79 thiscall methods). Layout follows player/src/om_player_api.h
// (package I7); the game keeps its own copy until a shared header exists.
//
// Implements: SPEC-003 (tables, owner context, inventory interface), SPEC-601 (item slot),
//             SPEC-782 Interface 1 (members of player.dll objects read by the game)
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sof_game.h"

// Common import table, 19 pointers / 76 bytes
struct PlayerCommonImport {
	cvar_t *(*Cvar_Get)(const char *name, const char *value, int flags, cvarcallback_t cb); // 0
	cvar_t *(*Cvar_Set)(const char *name, const char *value);                // 1
	void (*Cvar_SetValue)(const char *name, float value);                    // 2
	int (*Cmd_Argc)(void);                                                   // 3
	char *(*Cmd_Argv)(int n);                                                // 4
	char *(*Cmd_Args)(void);                                                 // 5
	void (*Com_Printf)(const char *fmt, ...);                                // 6
	void (*Com_sprintf)(char *dst, int size, const char *fmt, ...);          // 7
	void (*Com_DPrintf)(const char *fmt, ...);                               // 8
	int (*FS_LoadFile)(const char *path, void **buffer, int flag);           // 9
	void (*FS_FreeFile)(void *buffer);                                       // 10
	int (*irand)(int min, int max);                                          // 11
	cvar_t *ghl_specular;                                                    // 12
	void *newPlayerModelInfo;                                                // 13 (not filled by the game)
	int (*LoadGhoulFile)(const char *folder, const char *file, void **buffer); // 14
	int (*ReadLine)(int *remaining, char **cursor, char *line);              // 15
	void (*PreloadGSQ)(const char *folder, const char *name);                // 16
	int (*RegisterGSQSequences)(const char *folder, const char *name, ghoul::IGhoulObject *obj); // 17
	void (*TurnOffPartsFromGSQ)(const char *folder, const char *name, ghoul::IGhoulObject *obj,
	                            ghoul::IGhoulInst *inst);                    // 18
};

struct ItemSlot {
	int type;
	int count;
	int state;
	float nexttime;
};

class PlayerInventory;

// Server import table, 38 pointers / 152 bytes, supplied by the game
struct PlayerServerImport {
	ghoul::IGhoulSystem *(*GetGhoul)(void);                                  // 0
	float *time;                                                             // 1
	void (*FirePrimary)(void *unused, edict_t *owner, PlayerInventory *inv); // 2
	void (*FireAlt)(void *unused, edict_t *owner, PlayerInventory *inv);     // 3
	void (*ReloadStarted)(edict_t *owner);                                   // 4 (ERRATA: reload started)
	void (*DropWeapon)(edict_t *owner, int weapon, int rounds);              // 5
	void (*ItemUsed)(edict_t *owner, int itemtype);                          // 6
	void (*DropItem)(edict_t *owner, int itemtype, int count);               // 7
	void (*PrecacheAttack)(int attack);                                      // 8
	int (*EffectIndex)(const char *name);                                    // 9
	int (*SoundIndex)(const char *name);                                     // 10
	void *StartSound;                                                        // 11 (unused)
	int (*CarriesFlag)(edict_t *owner);                                      // 12 (ERRATA: carries CTF flag)
	float (*GetFireTimeA)(edict_t *owner);                                   // 13
	void (*ClearFireTimeA)(edict_t *owner);                                  // 14
	float (*GetFireTimeB)(edict_t *owner);                                   // 15
	void (*ClearFireTimeB)(edict_t *owner);                                  // 16
	void (*Cbuf_AddText)(const char *text);                                  // 17
	char *(*Cvar_BitInfo)(int flagmask);                                     // 18
	void (*PrintId)(edict_t *to, int id, ...);                               // 19
	const char *(*TextOfId)(int id);                                         // 20 (unused)
	int (*UseFlashpack)(edict_t *owner, ItemSlot *slot);                     // 21
	void (*Unused22)(edict_t *owner);                                        // 22
	int (*UseC4)(edict_t *owner, ItemSlot *slot);                            // 23
	int (*UseClaymore)(edict_t *owner, ItemSlot *slot);                      // 24
	int (*UseMedkit)(edict_t *owner, ItemSlot *slot);                        // 25
	int (*UseGrenade)(edict_t *owner, ItemSlot *slot);                       // 26
	int (*Goggles)(edict_t *owner, ItemSlot *slot);                          // 27
	void (*SZ_WriteByte)(sizebuf_t *sb, int c);                              // 28
	void (*SZ_WriteShort)(sizebuf_t *sb, int c);                             // 29
	void (*SZ_WriteLong)(sizebuf_t *sb, int c);                              // 30
	void (*ReliableByte)(int c, int clientnum);                              // 31
	void (*ReliableData)(const void *data, int length, int clientnum);       // 32
	void (*SZ_Init)(sizebuf_t *sb, void *data, int size);                    // 33
	void (*SZ_Clear)(sizebuf_t *sb);                                         // 34
	void (*SZ_Write)(sizebuf_t *sb, const void *data, int length);           // 35
	int (*WriteChunk)(int id, const void *data, int length);                 // 36
	int (*ReadChunk)(int id, void *buffer, int length, void **allocated);    // 37
};

// Export table, 12 entries / 48 bytes
struct PlayerExport {
	int version;                                                             // 0
	int *contextflag;                                                        // 1
	int (*Init)(void);                                                       // 2
	void (*Shutdown)(void);                                                  // 3
	void **weapondescs;                                                      // 4: 15 weapon descriptions
	int *firedprimary;                                                       // 5
	int *firedalt;                                                           // 6
	void (*PrecacheViewModels)(unsigned mask);                               // 7
	void (*ForgetViewModels)(void);                                          // 8
	PlayerInventory *(*NewInventory)(void);                                  // 9
	void (*DeleteInventory)(PlayerInventory *inv);                           // 10
	void (*RebuildAfterRestore)(PlayerInventory *inv);                       // 11
};

// Owner context, 52 bytes (SPEC-003, SPEC-602 Interface 4)
struct OwnerContext {
	PlayerInventory *inventory; // 0x00
	edict_t *owner;             // 0x04
	int attack;                 // 0x08
	int altattack;              // 0x0C
	int extra1;                 // 0x10
	int extra2;                 // 0x14
	int leanright;              // 0x18
	int leanleft;               // 0x1C
	int cinematicfreeze;        // 0x20
	int remoteview;             // 0x24
	float *kick;                // 0x28
	float frametime10;          // 0x2C
	int reloadoneshot;          // 0x30
};

// Inventory object: 79 thiscall methods in table order (SPEC-003)
class PlayerInventory {
public:
	virtual void RemoveAll(bool alsoflag) = 0;                                 // 0
	virtual void RemoveItemType(int itemtype) = 0;                             // 1
	virtual void DoPendingDrop() = 0;                                          // 2
	virtual void Think() = 0;                                                  // 3
	virtual bool HandleCommand(const char *cmd, int force) = 0;                // 4
	virtual void TopUpClips() = 0;                                             // 5
	virtual bool SwitchToWeapon(int weapon) = 0;                               // 6
	virtual void SwitchToSlot(int slot) = 0;                                   // 7
	virtual void SwitchToBest() = 0;                                           // 8
	virtual void RequestWeapon(int weapon) = 0;                                // 9
	virtual void RequestSlot(int slot) = 0;                                    // 10
	virtual void RequestBest() = 0;                                            // 11
	virtual bool AddAmmo(int ammotype, int amount) = 0;                        // 12
	virtual int WeaponMessageId(int weapon) = 0;                               // 13
	virtual bool AddAmmoForWeapon(int weapon, int amount) = 0;                 // 14
	virtual bool AddAmmoForWeapon2(int weapon, int amount) = 0;                // 15
	virtual bool GiveWeapon(int weapon, int cliprounds) = 0;                   // 16
	virtual int TotalWeight() = 0;                                             // 17
	virtual int WeaponWeight(int weapon) = 0;                                  // 18
	virtual bool AddItems(int itemtype, int count, int max) = 0;               // 19
	virtual int AddArmor(int delta) = 0;                                       // 20
	virtual bool HasItem(int itemtype) = 0;                                    // 21
	virtual bool OwnsWeapon(int weapon) = 0;                                   // 22
	virtual int CurrentWeapon() = 0;                                           // 23
	virtual int BestWeapon() = 0;                                              // 24
	virtual int SelectedItemType() = 0;                                        // 25
	virtual int SelectedItemCount() = 0;                                       // 26
	virtual int ReserveAmmo() = 0;                                             // 27
	virtual int ClipRounds() = 0;                                              // 28
	virtual int ClipSize() = 0;                                                // 29 (ERRATA: clip capacity)
	virtual int ClipSizeOf(int weapon) = 0;                                    // 30
	virtual int AbsorbArmor(int damage, float piercing, float fraction) = 0;   // 31 (SPEC-AMEND-005 G3.14)
	virtual int Armor() = 0;                                                   // 32
	virtual int ScopeActive() = 0;                                             // 33 (ERRATA: sniper scope)
	virtual void WeaponEvent(int arg) = 0;                                     // 34
	virtual int Holstered() = 0;                                               // 35 (ERRATA: holster flag)
	virtual void ToggleHolster() = 0;                                          // 36
	virtual int GetVarA() = 0;                                                 // 37
	virtual void SetVarA(int v) = 0;                                           // 38
	virtual int GetVarB() = 0;                                                 // 39
	virtual void SetVarB(int v) = 0;                                           // 40
	virtual int GetVarI() = 0;                                                 // 41
	virtual void SetVarI(int v) = 0;                                           // 42
	virtual int GetVarC() = 0;                                                 // 43
	virtual void SetVarC(int v) = 0;                                           // 44
	virtual int GetVarD() = 0;                                                 // 45
	virtual void SetVarD(int v) = 0;                                           // 46
	virtual int GetVarE() = 0;                                                 // 47
	virtual void SetVarE(int v) = 0;                                           // 48
	virtual char *GetTag() = 0;                                                // 49
	virtual void SetTag(const char *tag) = 0;                                  // 50
	virtual int GetVarF() = 0;                                                 // 51
	virtual void SetVarF(int v) = 0;                                           // 52
	virtual int GetVarG() = 0;                                                 // 53
	virtual void SetVarG(int v) = 0;                                           // 54
	virtual int GetVarH() = 0;                                                 // 55
	virtual void SetVarH(int v) = 0;                                           // 56
	virtual bool RequestDrop() = 0;                                            // 57
	virtual void WeaponHook1() = 0;                                            // 58
	virtual void ItemThink() = 0;                                              // 59
	virtual int NumWeapons() = 0;                                              // 60
	virtual void SetOwnerContext(OwnerContext *ctx) = 0;                       // 61
	virtual void DropAllButKept() = 0;                                         // 62
	virtual void WeaponHook2() = 0;                                            // 63
	virtual int LoadPersist(unsigned *weaponmask, int *health) = 0;            // 64
	virtual void SavePersist(bool addclips, int health) = 0;                   // 65
	virtual bool LoadInfo(unsigned *weaponmask, const char *info) = 0;         // 66
	virtual void WriteInfo(char *out, bool weapons, bool ammo, bool items, bool armor) = 0; // 67
	virtual void ClientClearEvents() = 0;                                      // 68
	virtual void ClientReadEvents() = 0;                                       // 69
	virtual void ClientApplyEvents() = 0;                                      // 70
	virtual void ServerClearEvents() = 0;                                      // 71
	virtual void ServerSendEvents() = 0;                                       // 72
	virtual void SetPredicting(int v) = 0;                                     // 73
	virtual int GetPredicting() = 0;                                           // 74
	virtual void WriteSave() = 0;                                              // 75
	virtual void ReadSave() = 0;                                               // 76
	virtual void ReadFullState(sizebuf_t *sb) = 0;                             // 77
	virtual void WriteFullState(int clientnum) = 0;                            // 78

protected:
	~PlayerInventory() {}
};

// Members of player.dll objects the game reads (SPEC-782 Interface 1)
#define INV_CURWEAPON_OFS 0x2C
#define INV_VIEWINST_OFS 0x14C
#define INV_SPREAD_OFS 0x270

// Weapon description: 32 virtual slots; the game calls 28 (0x70) and 29 (0x74)
class WeaponDesc {
public:
	virtual void v00() = 0; virtual void v01() = 0; virtual void v02() = 0; virtual void v03() = 0;
	virtual void v04() = 0; virtual void v05() = 0; virtual void v06() = 0; virtual void v07() = 0;
	virtual void v08() = 0; virtual void v09() = 0; virtual void v10() = 0; virtual void v11() = 0;
	virtual void v12() = 0; virtual void v13() = 0; virtual void v14() = 0; virtual void v15() = 0;
	virtual void v16() = 0; virtual void v17() = 0; virtual void v18() = 0; virtual void v19() = 0;
	virtual void v20() = 0; virtual void v21() = 0; virtual void v22() = 0; virtual void v23() = 0;
	virtual void v24() = 0; virtual void v25() = 0; virtual void v26() = 0; virtual void v27() = 0;
	virtual bool FiresFromMuzzle() = 0;                                        // 28 (0x70)
	virtual bool Converges() = 0;                                              // 29 (0x74)
	virtual void v30() = 0; virtual void v31() = 0;

protected:
	~WeaponDesc() {}
};
#define WDESC_SPREAD_PRI 0x1C
#define WDESC_SPREAD_ALT 0x20
#define WDESC_MAXSPREAD_PRI 0x24
#define WDESC_MAXSPREAD_ALT 0x28
#define WDESC_ATTACK_PRI 0xB8
#define WDESC_ATTACK_ALT 0xBC

#if defined(_M_IX86) || defined(__i386__) /* the SoF 32-bit x86 ABI (MSVC or GCC/Clang i386) */
static_assert(sizeof(PlayerCommonImport) == 76, "SPEC-003 common import is 76 bytes");
static_assert(sizeof(PlayerServerImport) == 152, "SPEC-003 server import is 152 bytes");
static_assert(sizeof(PlayerExport) == 48, "SPEC-003 export table is 48 bytes");
static_assert(sizeof(OwnerContext) == 52, "SPEC-003 owner context is 52 bytes");
static_assert(sizeof(ItemSlot) == 16, "SPEC-601 item slot is 16 bytes");
#endif

// weapon ids, ammo types, item types (SPEC-003)
enum {
	WP_NONE, WP_KNIFE, WP_PISTOL2, WP_PISTOL1, WP_MPISTOL, WP_ASSAULT, WP_SNIPER, WP_SLUGGER, WP_SHOTGUN,
	WP_MACHINEGUN, WP_ROCKET, WP_MPG, WP_FLAMEGUN, WP_HURTHAND, WP_THROWHAND
};
enum { AM_NONE, AM_KNIFE, AM_44, AM_9MM, AM_SHELLS, AM_556, AM_ROCKETS, AM_BATTERY, AM_FUEL, AM_SLUG };
enum { IT_NONE, IT_FLASHPACK, IT_C4, IT_GOGGLES, IT_CLAYMORE, IT_MEDKIT, IT_GRENADE, IT_CTFFLAG };
