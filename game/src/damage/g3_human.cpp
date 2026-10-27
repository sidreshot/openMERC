// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - the human bodies (male soldier, civilian/officer, female, armoured boss):
// hit location and damage on human bodies, set-up requests, animation choices, reactions, deaths and dying,
// voices, faces, weapons, armour and precache.
// Implements: SPEC-720 Behaviour 10, 11, 12 (armoured boss), SPEC-721 §2-§10 (as callers), SPEC-722 human part
#include "g3_local.h"

using namespace ghoul;

// weapon classes (SPEC-722 "Attack types and weapon models")
enum { WC_P = 1, WC_L = 2, WC_R = 4, WC_S = 8, WC_M = 16, WC_K = 32, WC_MP = 64 };

static int WeaponClass(int at)
{
	switch (at) {
	case 1: return WC_K;
	case 3: case 4: return WC_P;
	case 5: return WC_MP;
	case 6: case 12: case 18: return WC_M;
	case 7: return WC_R;
	case 9: case 11: case 16: case 17: case 29: return WC_S;
	case 14: return WC_L;
	}
	return 0;
}

static const char *RightHandModel(int at, int *weapon)
{
	*weapon = 0;
	switch (at) {
	case 1: case 2: *weapon = 1; return "w_knife";
	case 3: *weapon = 3; return "w_pistol1";
	case 4: *weapon = 2; return "w_pistol2";
	case 5: *weapon = 4; return "w_machinepistol";
	case 6: *weapon = 5; return "w_assault_rifle";
	case 7: case 8: *weapon = 6; return "w_sniperrifle";
	case 9: case 10: *weapon = 7; return "w_autoshotgun";
	case 11: *weapon = 8; return "w_shotgun";
	case 12: case 13: *weapon = 9; return "w_machinegun";
	case 14: *weapon = 10; return "w_rocket";
	case 16: case 17: case 29: *weapon = 11; return "w_mpg";
	case 18: case 19: *weapon = 12; return "w_flamethrower";
	}
	return NULL;
}

static int ClipRounds(int at)
{
	switch (at) {
	case 3: return 18;
	case 4: return 9;
	case 5: return 33;
	case 6: return 40;
	case 7: return 6;
	case 9: return 10;
	case 11: return 8;
	case 12: return 30;
	case 14: return 4;
	case 16: case 17: case 29: return 100;
	case 18: return 100000;
	}
	return 1;
}

static float AimSpread(int at)
{
	switch (at) {
	case 3: return 0.1f;
	case 5: return 0.4f;
	case 6: return 0.3f;
	case 9: case 11: case 12: case 16: case 17: case 18: case 29: return 0.2f;
	}
	return 0;
}

// the hint zone of SPEC-720 10.4: the last zone resolved on any human body
static int hintZone = -1;
void G3_ResetHint(void)
{
	hintZone = -1;
}

struct HumanVoiceDirs {
	char deathDir[64];
	bool touchGreetOK;
};

class G3HumanBody : public G3Body {
public:
	int rightType, leftType;
	char facePrefix[32];
	char faceSuffix[8];
	float blinkAt, breathAt;
	char voiceDir[64];
	int voiceNum;
	int deathType;
	char customDir[64];
	int numWake, numGreet;
	int greetIndex;
	float greetTime;
	int clip;
	float lastShot;
	bool justShot;
	float limbExpire[4];       // right leg, left leg, right arm, left arm
	bool armHitPending;
	bool committed, tumbling, tumbleFlag;
	bool deathNotified, armourDropped;
	float lastPainTime;
	bool firstHitTrigger;
	int scriptedKill;          // 0 none, 1 kill, 2 kill + spread
	GhoulID boltHandR, boltHandL;
	IGhoulInst *grenadeInst;
	char pendingVoice[128];
	float pendingVol, pendingAttn;
	bool hasPending;
	const G3Anim *prevAnim;
	float startledDone;
	int lastFidget;
	int chargeTick;

	G3HumanBody(edict_t *e, int fam) : G3Body(e, fam)
	{
		rightType = leftType = 0;
		facePrefix[0] = faceSuffix[0] = 0;
		blinkAt = breathAt = 0;
		voiceDir[0] = customDir[0] = 0;
		voiceNum = 1;
		deathType = 0;
		numWake = numGreet = 0;
		greetIndex = -1;
		greetTime = 0;
		clip = 1;
		lastShot = -999;
		justShot = false;
		for (int i = 0; i < 4; i++)
			limbExpire[i] = 0;
		armHitPending = false;
		committed = tumbling = tumbleFlag = false;
		deathNotified = armourDropped = false;
		lastPainTime = -999;
		firstHitTrigger = false;
		scriptedKill = 0;
		boltHandR = boltHandL = 0;
		grenadeInst = NULL;
		hasPending = false;
		prevAnim = NULL;
		startledDone = 0;
		lastFidget = -1;
		chargeTick = 0;
		switch (fam) {
		case BODY_ECTO: modelDir = "enemy/ecto"; break;
		case BODY_FEMALE: modelDir = "enemy/female"; break;
		default: modelDir = "enemy/meso"; break;
		}
		gore = G3_NewGore(this, GZ_NUM_HUMAN);
		SetupScales();
	}

	void SetupScales(void)
	{
		if (!gore)
			return;
		if (gore->numZones < 26) {
			G3_Printf("couldn't setup gzone damage scales!!!\n");
			return;
		}
		static const float s[26] = {10, 10, 10, 1, 1, 1, 1, 1, 1, 0.5f, 0.5f, 0.5f, 0.5f, 1, 1, 1, 0.5f, 0.5f, 0.5f,
			0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1, 1};
		for (int i = 0; i < 26; i++)
			gore->zones[i].scale = s[i];
	}

	int WClass(void) { return WeaponClass(rightType); }
	int RightHandType(void) override { return rightType; }
	int LeftHandType(void) override { return leftType; }
	int ProtectionClass(void) override { return gore ? gore->armourType : 0; }
	bool Armed(void) { return rightType != 0 && WClass() != 0; }
	int Posture(void) { return cur ? cur->posture : POSTURE_STAND; }

	const G3Anim *A(const char *n) { return Anim_Human(n); }
	const G3Anim *Avail(const char *n)
	{
		const G3Anim *a = Anim_Human(n);
		return (a && HasAnim(a)) ? a : NULL;
	}
	// the last row with a name when it has the aim flag (the machine pistol fire stances, appended to the table),
	// otherwise the name lookup: a machine pistol fires from these stances (SPEC-722 attack choice, MP entries)
	const G3Anim *AvailFire(const char *n)
	{
		for (int i = g3_numHumanAnims - 1; i >= 0; i--)
			if (!Q_stricmp(g3_humanAnims[i].name, n) && (g3_humanAnims[i].flags & ANIMF_AIM))
				return HasAnim(&g3_humanAnims[i]) ? &g3_humanAnims[i] : NULL;
		return Avail(n);
	}
	const G3Anim *First(std::initializer_list<const char *> names)
	{
		for (const char *n : names)
			if (n) {
				const G3Anim *a = Avail(n);
				if (a)
					return a;
			}
		return NULL;
	}

	// token helpers by weapon class
	const char *Tok(int wc, const char *mrs, const char *ms, const char *r, const char *l, const char *p,
	                const char *pk)
	{
		if ((wc & (WC_M | WC_R | WC_S)) && mrs)
			return mrs;
		if ((wc & (WC_M | WC_S)) && ms)
			return ms;
		if ((wc & WC_R) && r)
			return r;
		if ((wc & WC_L) && l)
			return l;
		if ((wc & (WC_P | WC_MP)) && p)
			return p;
		return pk;
	}

	bool LimbActive(int i)
	{
		if (Dmg_Toughness() >= 4) {
			limbExpire[i] = 0;
			return false;
		}
		return limbExpire[i] > G3_Time();
	}
	int LimbInjury(void) override
	{
		int f = 0;
		for (int i = 0; i < 4; i++)
			if (LimbActive(i))
				f |= 1 << i;
		return f;
	}

	// ---------------------------------------------------------------------------------------------------
	// set-up requests
	// ---------------------------------------------------------------------------------------------------
	bool SetRightHand(int at) override
	{
		IGhoulInst *inst = Inst();
		GhoulID b = G3G_Part(inst, "wbolt_hand_r");
		boltHandR = b;
		if (b)
			G3G_RemoveChildrenOnBolt(inst, b);
		int weapon;
		const char *m = RightHandModel(at, &weapon);
		rightType = 0;
		if (!m || !b)
			return false;
		const char *tob = (at == 9 || at == 10 || at == 16 || at == 17 || at == 29) ? "_to_wbolt_hand_r"
		                                                                          : "to_wbolt_hand_r";
		IGhoulInst *w = G3G_AttachByName(inst, "wbolt_hand_r", "Enemy/Bolt", m, tob, 1.0f);
		if (!w)
			return false;
		if (!(at == 5 && G3_ClassIs(ent, "m_x_mmerc")))
			WeaponPrecacheAttack(at);
		rightType = at;
		return true;
	}

	void SetLeftHand(int at) override
	{
		IGhoulInst *inst = Inst();
		boltHandL = G3G_Part(inst, "wbolt_hand_l");
		leftType = at;
		if (at == 27) {
			G3G_AttachByName(inst, "wbolt_hand_l", "Enemy/Bolt", "w_machete", "to_wbolt_hand_l", 1.0f);
			return;
		}
		if (at && !Avail("std_tgrenade_n_a_n") && !Avail("cch_tlobgrenade_n_a_n") && !Avail("std_tstar_fwd_a_n"))
			leftType = 0;
	}

	void SetFace(const char *prefix) override
	{
		strncpy(facePrefix, prefix ? prefix : "", sizeof facePrefix - 1);
		facePrefix[sizeof facePrefix - 1] = 0;
		faceSuffix[0] = 0;
		blinkAt = G3_Time() + G3_flrand(3, 5);
		if (gore && facePrefix[0])
			gore->RegisterHalfHead(facePrefix); // SPEC-721 1.5 when the face is set up after the zones
	}

	void SetVoice(const char *name, int count, int dtype, const char *custom) override
	{
		voiceDir[0] = 0;
		if (name && *name)
			Com_sprintf(voiceDir, sizeof voiceDir, "Enemy/%s", name);
		if (count < 1)
			count = 1;
		voiceNum = G3_CreatePak() ? count : G3_irand(1, count);
		deathType = dtype;
		strncpy(customDir, custom ? custom : "", sizeof customDir - 1);
		customDir[sizeof customDir - 1] = 0;
		voiceCount = count;
	}
	int voiceCount = 1;

	const char *LevelDir(void)
	{
		const char *m = G3_MapName();
		if (!Q_stricmp(m, "tsr2"))
			return "TSR2";
		static const char *const known[] = {"tut1", "tsr2", "trn1", "kos1", "kos2", "kos3", "sib1", "sib2", "sib3",
			"irq1a", "irq2a", "irq3a", "nyc1", "nyc2", "nyc3", "sud1", "sud2", "sud3", "jpn1", "jpn2", "jpn3",
			"irq1b", "irq2b", "irq3b", "ger1", "ger2", "ger3", "ger4", "arm1", "arm2", "arm3", NULL};
		for (int i = 0; known[i]; i++)
			if (!Q_stricmp(m, known[i]))
				return m;
		/* a map whose name contains a stock level name uses that level's voices: the original plays
		 * enemy/ni1/jpn3/wake1.wav on g4jpn3d (mode A, ninja test n1A/n2A); OpenMerc fell back to TSR1, found no
		 * ni1/TSR1 wake files and stayed silent. SR-MODEB-06 asks for the exact rule. */
		char low[64];
		strncpy(low, m, sizeof low - 1);
		low[sizeof low - 1] = 0;
		for (char *c = low; *c; c++)
			if (*c >= 'A' && *c <= 'Z')
				*c += 32;
		for (int i = 0; known[i]; i++)
			if (strstr(low, known[i]))
				return i == 1 ? "TSR2" : known[i];
		return "TSR1";
	}

	const char *DeathDir(void)
	{
		const char *m = G3_MapName();
		if (deathType == 7)
			return customDir[0] ? customDir : NULL;
		if (!Q_stricmp(m, "tut1") || !Q_stricmp(m, "arm1") || !Q_stricmp(m, "arm2") || !Q_stricmp(m, "arm3"))
			return NULL;
		if (deathType == 5)
			return Q_stricmp(m, "jpn2") ? "enemy/dth/fem" : NULL;
		static const char *const russ[] = {"tsr2", "kos1", "kos2", "kos3", "sib1", "sib2", "sib3", NULL};
		static const char *const arab[] = {"trn1", "irq1a", "irq2a", "irq3a", "irq1b", "irq2b", "irq3b", "sud1",
			"sud2", "sud3", NULL};
		static const char *const jpn[] = {"jpn1", "jpn2", "jpn3", NULL};
		static const char *const rad[] = {"ger1", "ger2", "ger3", "ger4", NULL};
		for (int i = 0; russ[i]; i++)
			if (!Q_stricmp(m, russ[i]))
				return "enemy/dth/russ";
		for (int i = 0; arab[i]; i++)
			if (!Q_stricmp(m, arab[i]))
				return "enemy/dth/arab";
		for (int i = 0; jpn[i]; i++)
			if (!Q_stricmp(m, jpn[i]))
				return "enemy/dth/jpn";
		for (int i = 0; rad[i]; i++)
			if (!Q_stricmp(m, rad[i]))
				return "enemy/dth/rad";
		return "enemy/dth/skin";
	}

	void Precache(void) override
	{
		static const char *const snd[] = {"impact/gore/fall1.wav", "impact/gore/fall2.wav", "impact/gore/fall3.wav",
			"impact/gore/impact1.wav", "impact/gore/impact2.wav", "impact/gore/impact3.wav",
			"impact/gore/limbfall.wav", "impact/gore/sizzle.wav", "impact/gore/hitflesh.wav",
			"impact/gore/headexp.wav", "Weapons/gren/throw.wav", "impact/armor/hit.wav", NULL};
		for (int i = 0; snd[i]; i++)
			G3_SoundIndex(snd[i]);
		const char *dd = DeathDir();
		if (dd) {
			struct { const char *e; int n; } gv[] = {{"Burn", 1}, {"Choke", 2}, {"Crotch", 2}, {"Hit", 3},
				{"Scream", 8}, {"Writhe", 7}, {"Limp", 3}, {"Getup", 1}, {"Disarm", 1}};
			for (auto &g : gv)
				for (int k = 1; k <= g.n; k++) {
					char s[128];
					Com_sprintf(s, sizeof s, "%s/%s%d.wav", dd, g.e, k);
					G3_SoundIndex(s);
				}
		}
		// wake and greeting voices: count files until one is missing
		numWake = numGreet = 0;
		if (voiceDir[0]) {
			int lo = G3_CreatePak() ? 1 : voiceNum, hi = G3_CreatePak() ? voiceCount : voiceNum;
			for (int n = lo; n <= hi; n++) {
				for (int i = 1;; i++) {
					char s[160];
					Com_sprintf(s, sizeof s, "sound/%s%d/%s/Wake%d.wav", voiceDir, n, LevelDir(), i);
					if (!gix.FS_FileExists || !gix.FS_FileExists(s))
						break;
					G3_SoundIndex(s + 6);
					if (G3_GoreTest())
						G3_Printf("voice cache: %s\n", s);
					if (n == voiceNum)
						numWake = i;
				}
				for (int i = 1;; i++) {
					char s[160];
					Com_sprintf(s, sizeof s, "sound/%s%d/%s/Greet%d.adp", voiceDir, n, LevelDir(), i);
					if (!gix.FS_FileExists || !gix.FS_FileExists(s))
						break;
					G3_SoundIndex(s + 6);
					if (G3_GoreTest())
						G3_Printf("voice cache: %s\n", s);
					if (n == voiceNum)
						numGreet = i;
				}
			}
		}
		const char *dir = G3G_ModelDirOfInst(Inst());
		if (dir)
			modelDir = dir; // sequences are looked up in the model's own directory (player models too)
		static const char *const fx[] = {"gore/stumpblood", "gore/headcloud", "gore/bloodcloud", "gore/bloodcloud_s",
			"gore/noblood", "gore/noblood_s", "gore/gibbody", "spark2", "weakspark", "armorhit", "gore/bloodpool",
			"gore/danglyblood", "gore/hugeburst2", "environ/gb_exitw", NULL};
		for (int i = 0; fx[i]; i++)
			G3_EffectIndex(fx[i]);
		if (!Q_strncasecmp(G3_MapName(), "sib", 3))
			G3_EffectIndex("enemy/breathe");
		if (rightType)
			WeaponPrecacheAttack(rightType);
		if (leftType)
			WeaponPrecacheAttack(leftType);
		clip = ClipRounds(rightType);
		if (gore)
			gore->Setup();
		RegisterEvents();
		if (!ent->client) {
			// note `sound` arguments of the model are registered as sounds
			IGhoulObject *o = G3G_Object(Inst());
			if (o) {
				GhoulID n = o->FindNote("sound");
				if (n)
					o->EnumerateNotes(&soundEnum, n);
			}
		}
	}
	class SoundEnum : public IGhoulCallback {
	public:
		bool Event(uint32_t, void *, float, const void *data) override
		{
			if (data && *(const char *)data) {
				char s[128];
				Com_sprintf(s, sizeof s, "%s.wav", (const char *)data);
				G3_SoundIndex(s);
			}
			return true;
		}
	} soundEnum;

	void SetArmour(int type) override
	{
		if (ent->client || !gore || gore->numZones < 26)
			return;
		switch (type) {
		case 4:
			ent->health = (int)(ent->health * 1.5f);
			ent->max_health = (int)(ent->max_health * 1.5f);
			return;
		case 5:
			ent->health *= 2;
			ent->max_health *= 2;
			return;
		}
		if (type < 0 || type > 7)
			return;
		gore->SetArmour(type, Gore_ArmourPoints(this));
	}

	void StripArmour(void) override
	{
		if (gore)
			gore->ClearArmour();
	}

	int ArmourPercent(void) override { return (int)ArmourPercentF(); }
	float ArmourPercentF(void)
	{
		if (!gore)
			return 0;
		int t = gore->armourType;
		if (t != 1 && t != 2 && t != 3 && t != 6 && t != 7)
			return 0;
		int count = 0;
		static const int z1[] = {3, 4, 5, 6, 7, 8, 13, 14};
		if (t == 1)
			count = 8;
		else if (t == 2 || t == 6)
			count = 23;
		else
			count = 26;
		(void)z1;
		float pct = (float)gore->RemainingArmour() / (count * 300.0f) * 100.0f;
		if (t == 6) {
			pct *= 0.25f;
			if (pct > 20)
				pct = 20;
		}
		return pct;
	}

	// ---------------------------------------------------------------------------------------------------
	// hands
	// ---------------------------------------------------------------------------------------------------
	int HandBolt(GhoulID b) override
	{
		IGhoulInst *inst = Inst();
		if (!boltHandR)
			boltHandR = G3G_Part(inst, "wbolt_hand_r");
		if (!boltHandL)
			boltHandL = G3G_Part(inst, "wbolt_hand_l");
		if (b && b == boltHandR)
			return 0;
		if (b && b == boltHandL)
			return 1;
		return -1;
	}
	void ForgetHand(int hand) override
	{
		if (hand == 0)
			rightType = 0;
		else if (hand == 1)
			leftType = 0;
	}
	void ClearHand(int hand) override
	{
		HandBolt(0);
		GhoulID b = hand == 0 ? boltHandR : (hand == 1 ? boltHandL : 0);
		if (b)
			G3G_RemoveChildrenOnBolt(Inst(), b);
		if (hand == 1 && grenadeInst)
			grenadeInst = NULL;
		ForgetHand(hand);
	}
	bool NoDrop(void) override { return false; }
	void DropHandWeapon(int hand) override
	{
		IGhoulInst *inst = Inst();
		GhoulID b = hand == 0 ? boltHandR : boltHandL;
		IGhoulInst *w = G3G_ChildOnBolt(inst, b);
		if (!w)
			return;
		G3Hit h;
		memset(&h, 0, sizeof h);
		h.targ = ent;
		VectorCopy(ent->s.origin, h.point);
		h.damage = 1000;
		if (gore)
			gore->DropBoltOn(w, h, !Q_strncasecmp(G3G_ModelName(w->GetGhoulObject()), "w_", 2));
		else
			G3G_RemoveChild(Inst(), w);
		ForgetHand(hand);
	}
	void DropWeapons(void) override
	{
		DropHandWeapon(0);
		DropHandWeapon(1);
		rightType = 0;
	}

	// ---------------------------------------------------------------------------------------------------
	// playback additions
	// ---------------------------------------------------------------------------------------------------
	void SwapPair(const char *open, const char *closed, bool wantOpen)
	{
		IGhoulInst *inst = Inst();
		GhoulID o = G3G_Part(inst, open), c = G3G_Part(inst, closed);
		if (!o || !c)
			return;
		GhoulID shown = inst->IsPartOn(o) ? o : (inst->IsPartOn(c) ? c : 0);
		if (!shown)
			return;
		inst->SetPartOnOff(o, wantOpen);
		inst->SetPartOnOff(c, !wantOpen);
	}

	void PrePlay(const G3Anim *a, bool changed) override
	{
		(void)changed;
		IGhoulInst *inst = Inst();
		if (!inst)
			return;
		bool ro = (a->flags & ANIMF_RHAND_OPEN) != 0, lo = (a->flags & ANIMF_LHAND_OPEN) != 0;
		if (family != BODY_ECTO) {
			SwapPair("_ropen", "_rclosed", ro);
			SwapPair("_rbossopen", "_rbossclosed", ro);
			SwapPair("_rhand_open", "_rhand_closed", ro);
		}
		SwapPair("_lopen", "_lclosed", lo);
		SwapPair("_lbossopen", "_lbossclosed", lo);
		SwapPair("_lhand_open", "_lhand_closed", lo);
		if (a->flags & ANIMF_GRENADE) {
			if (!grenadeInst)
				grenadeInst = G3G_AttachByName(inst, "wbolt_hand_l", "Items/Projectiles", "grenade",
				                               "to_wbolt_hand_l", 1.0f);
		} else if (grenadeInst) {
			if (G3G_IsDescendant(inst, grenadeInst))
				G3G_RemoveChild(inst, grenadeInst);
			grenadeInst = NULL;
		}
		if ((a->flags & ANIMF_LEAK) && !g3locks.textures)
			G3FX_OnBolt("environ/takeleak", ent, G3G_Part(inst, "gbolt_groin"));
		prevAnim = cur;
	}

	// ---------------------------------------------------------------------------------------------------
	// voices
	// ---------------------------------------------------------------------------------------------------
	void QueueVoice(const char *path, float vol, float attn)
	{
		strncpy(pendingVoice, path, sizeof pendingVoice - 1);
		pendingVoice[sizeof pendingVoice - 1] = 0;
		pendingVol = vol;
		pendingAttn = attn;
		hasPending = true;
	}

	void GeneralVoice(const char *event, int variants, float volume) override
	{
		const char *dd = DeathDir();
		if (!dd || muted)
			return;
		bool choke = !Q_stricmp(event, "choke");
		if (!voiceEnabled && !choke)
			return;
		if (gore && gore->voiceDisabled && !choke)
			return;
		char s[128];
		Com_sprintf(s, sizeof s, "%s/%s%d.wav", dd, event, G3_irand(1, variants));
		if (!Q_stricmp(event, "Limp"))
			volume = 0.75f;
		QueueVoice(s, volume, 2.0f);
		if (g3hooks.musicIntensity)
			g3hooks.musicIntensity(0.05f);
		if (G3_GoreTest())
			G3_Printf("general voice sound: %s\n", s);
	}

	bool CanTalk(void)
	{
		return voiceDir[0] && !(owner && owner->ScriptedSilence()) && !G3_Cinematic() && voiceEnabled && !muted &&
		       gore && !(gore->voiceDisabled);
	}

	void TacticalVoice(const char *event, int variants) override
	{
		static float lastTactical = -999;
		static char lastEvent[32];
		if (!CanTalk() || G3_Time() - lastTactical < 2.0f || !Q_stricmp(lastEvent, event))
			return;
		char s[160];
		if (variants > 0)
			Com_sprintf(s, sizeof s, "%s%d/%s/%s%d.wav", voiceDir, voiceNum, LevelDir(), event,
			            G3_irand(1, variants));
		else
			Com_sprintf(s, sizeof s, "%s%d/%s/%s.wav", voiceDir, voiceNum, LevelDir(), event);
		if (G3_GoreTest())
			G3_Printf("voice sound: %s\n", s);
		if (!gix.FS_FileExists || !gix.FS_FileExists(s))
			return;
		lastTactical = G3_Time();
		strncpy(lastEvent, event, sizeof lastEvent - 1);
		QueueVoice(s, 1.0f, 1.6f);
	}

	void Wake(float volume) override
	{
		static float lastWake = -999;
		static int lastIdx = -1;
		if (!CanTalk() || ent->health <= 0 || numWake < 1 || G3_Time() - lastWake < 2.0f)
			return;
		int i = G3_irand(1, numWake);
		if (numWake > 1)
			while (i == lastIdx)
				i = G3_irand(1, numWake);
		lastIdx = i;
		lastWake = G3_Time();
		wakeTime = G3_Time();
		char s[160];
		Com_sprintf(s, sizeof s, "%s%d/%s/Wake%d.wav", voiceDir, voiceNum, LevelDir(), i);
		if (G3_GoreTest())
			G3_Printf("wake sound: %s\n", s);
		QueueVoice(s, volume, 1.6f);
	}
	float wakeTime = -999;

	bool Greet(edict_t *by, bool touch)
	{
		static int lastGreet = -1;
		if (ent->health <= 0 || G3_Time() < greetTime)
			return false;
		if (touch) {
			const char *m = G3_MapName();
			if (!Q_stricmp(m, "tut1") || !Q_stricmp(m, "arm1") || !Q_stricmp(m, "arm2") || !Q_stricmp(m, "arm3"))
				return false;
		}
		if (Armed() && by && !OnSameTeam(by, ent) && by->client)
			return false;
		if (ent->health == 9999) {
			greetTime = G3_Time() + 10.0f;
			return false;
		}
		int st = (expr == EXPR_AFRAID || expr == EXPR_PAIN) ? EXPR_TALK_AFRAID
		                                                     : (expr == EXPR_NEUTRAL ? EXPR_TALK : EXPR_TALK_ALT);
		SetExpression(st, 2.0f, false);
		if (numGreet > 0 && voiceDir[0]) {
			if (greetIndex < 0) {
				greetIndex = G3_irand(1, numGreet);
				if (numGreet > 1)
					while (greetIndex == lastGreet)
						greetIndex = G3_irand(1, numGreet);
			}
			lastGreet = greetIndex;
			char s[160];
			Com_sprintf(s, sizeof s, "%s%d/%s/Greet%d.adp", voiceDir, voiceNum, LevelDir(), greetIndex);
			if (G3_GoreTest())
				G3_Printf("greet sound: %s\n", s);
			QueueVoice(s, 0.9f, 1.6f);
		}
		greetTime = G3_Time() + 10.0f;
		return true;
	}
	void OnUsed(edict_t *user) override { Greet(user, false); }
	void OnTouched(edict_t *toucher) override { Greet(toucher, true); }

	// ---------------------------------------------------------------------------------------------------
	// faces, breath, weapon charge (per think)
	// ---------------------------------------------------------------------------------------------------
	void UpdateFace(void)
	{
		IGhoulInst *inst = Inst();
		float now = G3_Time();
		if (facePrefix[0] && inst) {
			const char *suf = "n";
			char buf[8];
			auto talk = [&](char base) -> const char * {
				static const char *const n3[] = {"nt3", "nt2", "nt1"};
				static const char *const m3[] = {"mt3", "mt2", "mt1"};
				static const char *const f3[] = {"ft3", "ft2", "ft1"};
				const char *const *t = base == 'n' ? n3 : (base == 'm' ? m3 : f3);
				int pick = -1;
				if (G3_irand(0, 3) == 0)
					pick = 0;
				else if (G3_irand(0, 2) == 0)
					pick = 1;
				else if (G3_irand(0, 1) == 0)
					pick = 2;
				if (pick >= 0) {
					char nm[64];
					Com_sprintf(nm, sizeof nm, "%s%s", facePrefix, t[pick]);
					if (G3G_Skin(inst, G3G_Material(inst, "f"), nm))
						return t[pick];
				}
				buf[0] = base;
				buf[1] = 0;
				return buf;
			};
			switch (expr) {
			case EXPR_DEAD: suf = "d"; break;
			case EXPR_PAIN: suf = "p"; break;
			case EXPR_ALT: suf = "m"; break;
			case EXPR_AFRAID: suf = "f"; break;
			case EXPR_TALK:
				suf = talk('n');
				if (now > blinkAt)
					blinkAt = now + G3_flrand(3, 5);
				break;
			case EXPR_TALK_ALT: suf = talk('m'); break;
			case EXPR_TALK_AFRAID: suf = talk('f'); break;
			default:
				if (now > blinkAt) {
					suf = "b";
					blinkAt = now + G3_flrand(3, 5);
				}
				break;
			}
			if (strcmp(suf, faceSuffix)) {
				strncpy(faceSuffix, suf, sizeof faceSuffix - 1);
				char nm[64];
				Com_sprintf(nm, sizeof nm, "%s%s", facePrefix, suf);
				if (!G3G_SetSkinByName(inst, "f", nm, 0)) {
					Com_sprintf(nm, sizeof nm, "%sn", facePrefix);
					G3G_SetSkinByName(inst, "f", nm, 0);
				}
			}
		}
		// breath on cold maps
		const char *m = G3_MapName();
		if (expr != EXPR_DEAD && (!Q_stricmp(m, "tsr2") || !Q_stricmp(m, "sib1") || !Q_stricmp(m, "sib2") ||
		    !Q_stricmp(m, "sib3")) && now >= breathAt && ent->health > 0) {
			G3FX_OnBolt("enemy/breathe", ent, G3G_Part(inst, "sbolt_mouth"));
			if (expr == EXPR_AFRAID || expr == EXPR_PAIN)
				breathAt = now + G3_flrand(1.0f, 1.5f);
			else if (expr == EXPR_ALT)
				breathAt = now + G3_flrand(1.25f, 2.0f);
			else
				breathAt = now + G3_flrand(2.5f, 4.5f);
		}
	}

	bool MuzzleEffect(const char *effect) override
	{
		IGhoulInst *w = G3G_ChildOnBolt(Inst(), boltHandR);
		GhoulID flash = w ? G3G_Part(w, "flash") : 0;
		if (!flash)
			return false;
		G3FX_OnBolt(effect, ent, flash, w);
		return true;
	}
	void WeaponCharge(void)
	{
		IGhoulInst *inst = Inst();
		IGhoulInst *w = G3G_ChildOnBolt(inst, boltHandR);
		if (!w || ent->health <= 0)
			return;
		GhoulID flash = G3G_Part(w, "flash");
		if (rightType == 16 || rightType == 17) {
			if (G3_Time() - lastShot > 1.0f) {
				G3FX_OnBolt("weapons/othermz/mpgcharge", ent, flash, w);
				if (++chargeTick % 5 == 0)
					G3FX_OnEnt("weapons/othermz/mpgcharge2", ent);
			}
		} else if (rightType == 18) {
			G3FX_OnBolt("weapons/othermz/flamecharge", ent, flash, w);
			ent->s.sound = (G3_Time() - lastShot < 0.5f) ? G3_SoundIndex("Weapons/flamegun/Fire.wav") : 0;
		}
	}

	void Think(void) override
	{
		G3Body::Think();
		if (hasPending) {
			G3_Sound(ent, CHAN_VOICE, pendingVoice, pendingVol, pendingAttn);
			hasPending = false;
		}
		if (ent->client)
			return;
		UpdateFace();
		WeaponCharge();
		if (ent->health <= 0 && cur && cur->cls == ANIMCLS_DEATH)
			CorpseState();
	}

	// ---------------------------------------------------------------------------------------------------
	// heels (footsteps and limp voices)
	// ---------------------------------------------------------------------------------------------------
	void OnHeel(int side, float height) override
	{
		if (owner)
			owner->OnHeel(side, height);
		if (ent->health <= 0 || !owner || owner->MoveValue() <= 9)
			return;
		if (height - owner->ReferenceHeight() < 7) {
			bool &down = side == 0 ? heelDownR : heelDownL;
			if (down)
				return;
			down = true;
			G3_AddEvent(ent, (gore && gore->armourType == 3) ? 14 : 4, 0);
			bool limpR = cur == Anim_Human("std_plimp_lt_n_n"), limpL = cur == Anim_Human("std_plimp_rt_n_n");
			if (((side == 0 && limpR) || (side == 1 && limpL)) && G3_irand(0, 1) == 0)
				GeneralVoice("Limp", 3, 0.75f);
		} else {
			(side == 0 ? heelDownR : heelDownL) = false;
		}
	}
	bool heelDownR = false, heelDownL = false;

	// ---------------------------------------------------------------------------------------------------
	// hit location (SPEC-720 10)
	// ---------------------------------------------------------------------------------------------------
	bool FromBehind(const G3Hit &h)
	{
		vec3_t d, f, l, u;
		VectorSubtract(ent->s.origin, h.origin, d);
		d[2] = 0;
		if (VectorNormalize(d) == 0)
			return false;
		G3_EntityAxes(ent, f, l, u);
		f[2] = 0;
		VectorNormalize(f);
		return DotProduct(d, f) > 0.707f;
	}

	int SpreadFromHint(int hz, const G3Hit &h, int *out)
	{
		bool behind = FromBehind(h);
		switch (hz) {
		case 0: case 1: out[0] = 2; return 1;
		case 2: out[0] = behind ? 1 : 0; return 1;
		case 3: out[0] = 5; out[1] = 7; out[2] = 9; return 3;
		case 4: out[0] = 6; out[1] = 8; out[2] = 9; return 3;
		case 5: out[0] = 3; out[1] = 7; out[2] = 10; return 3;
		case 6: out[0] = 4; out[1] = 8; out[2] = 10; return 3;
		case 7: out[0] = 3; out[1] = 5; out[2] = 13; return 3;
		case 8: out[0] = 4; out[1] = 6; out[2] = 14; return 3;
		case 9: out[0] = 3; out[1] = 11; return 2;
		case 10: out[0] = 5; out[1] = 12; return 2;
		case 11: out[0] = 9; return 1;
		case 12: out[0] = 10; return 1;
		case 13: case 24: out[0] = 3; out[1] = 7; out[2] = 15; return 3;
		case 14: case 25: out[0] = 6; out[1] = 8; out[2] = 4; return 3;
		case 15: out[0] = 15; return 1;
		case 16: out[0] = 15; out[1] = 20; out[2] = 13; return 3;
		case 17: out[0] = 15; out[1] = 20; out[2] = 14; return 3;
		case 18: out[0] = 15; out[1] = 21; out[2] = 13; return 3;
		case 19: out[0] = 15; out[1] = 21; out[2] = 14; return 3;
		case 20: out[0] = behind ? 17 : 16; out[1] = 22; return 2;
		case 21: out[0] = behind ? 19 : 18; out[1] = 23; return 2;
		case 22: out[0] = 20; return 1;
		case 23: out[0] = 21; return 1;
		}
		return 0;
	}

	// 0 front, 1 behind, 2 right side, 3 left side
	// 0 front, 1 behind, 2 right, 3 left: c = horizontal direction · target forward (> 0.707 behind, < -0.707
	// front), "right" when the direction · right <= 0. The first estimator (and the torso blast) use the direction
	// from the damage origin to the target's origin, the second estimator the one to the hit point (SPEC-AMEND-006
	// G3.21).
	int Side(const G3Hit &h, bool toPoint = false)
	{
		vec3_t d, f, l, u;
		VectorSubtract(toPoint ? h.point : ent->s.origin, h.origin, d);
		d[2] = 0;
		if (VectorNormalize(d) == 0)
			return 0;
		G3_EntityAxes(ent, f, l, u);
		f[2] = l[2] = 0;
		VectorNormalize(f);
		VectorNormalize(l);
		float df = DotProduct(d, f);
		if (df > 0.707f)
			return 1;
		if (df < -0.707f)
			return 0;
		return DotProduct(d, l) >= 0 ? 2 : 3; // d · right = -(d · left) <= 0
	}

	// shuffle `size` candidates and take the first k (k = irand(1, size))
	int Draw(const int *set, int size, int *out)
	{
		int pool[5];
		for (int i = 0; i < size; i++)
			pool[i] = set[i];
		int k = G3_irand(1, size), cnt = 0;
		for (int i = 0; i < k; i++) {
			int j = G3_irand(i, size - 1);
			int t = pool[i];
			pool[i] = pool[j];
			pool[j] = t;
			out[cnt++] = pool[i];
		}
		return cnt;
	}

	// head chance n (SPEC-AMEND-006 G3.21, both estimators): 6 when the damage origin is above the target's origin;
	// else 4 when the hit point is above the damage origin and less than 10 units below the top of the target's box;
	// else 10
	int HeadChanceN(const G3Hit &h)
	{
		if (h.origin[2] > ent->s.origin[2])
			return 6;
		if (h.point[2] > h.origin[2] && ent->absmax[2] - h.point[2] < 10)
			return 4;
		return 10;
	}

	// 10.5 estimate (no hint). The second estimator applies while the current record's posture is crouched (1),
	// prone (2) or lying (5, 6, 7) (SPEC-AMEND-006 G3.21): no leg case, sets right 7 3 16 11 4 / left 7 5 18 12 6 /
	// front 7 5 3 13 16 (1-5 drawn), behind as the first; head chance 1/(n + 1), zone 0 replacing a fifth zone.
	int Estimate(const G3Hit &h, int *out, int *exitz)
	{
		int post = cur ? cur->posture : POSTURE_STAND;
		bool second = post == POSTURE_CROUCH || post == POSTURE_PRONE || post == POSTURE_FACEDOWN ||
		              post == POSTURE_ONBACK || post == POSTURE_ONSIDE;
		int n = HeadChanceN(h);
		static const int torsoB[4] = {8, 6, 4, 14};
		int cnt;
		if (second) {
			static const int r2[5] = {7, 3, 16, 11, 4}, l2[5] = {7, 5, 18, 12, 6}, f2[5] = {7, 5, 3, 13, 16};
			int side = Side(h, true);
			if (side == 1) {
				cnt = Draw(torsoB, 4, out);
				*exitz = 7;
			} else {
				cnt = Draw(side == 2 ? r2 : side == 3 ? l2 : f2, 5, out);
				*exitz = 8;
			}
			if (G3_irand(0, n) == 0) {
				int hz = side == 1 ? 1 : 0;
				if (cnt == 5 && hz == 0)
					out[4] = 0;
				else
					out[cnt++] = hz;
			}
			return cnt;
		}
		int side = Side(h);
		float dz = h.point[2] - ent->s.origin[2];
		static const int legsB[4] = {17, 20, 21, 19}, legsF[4] = {16, 20, 21, 18};
		static const int torsoR[4] = {7, 9, 14, 4}, torsoL[4] = {7, 10, 14, 6}, torsoF[4] = {7, 5, 3, 13};
		const int *set;
		if (dz < 0) {
			// below the origin: legs
			set = side == 1 ? legsB : legsF;
			*exitz = side == 1 ? 16 : 17;
		} else if (side == 1) {
			set = torsoB;
			*exitz = 7;
		} else if (side == 2) {
			set = torsoR;
			*exitz = 8;
		} else if (side == 3) {
			set = torsoL;
			*exitz = 8;
		} else {
			set = torsoF;
			*exitz = 8;
		}
		cnt = Draw(set, 4, out);
		if (G3_irand(0, n - 1) == 0)
			out[cnt++] = side == 1 ? 1 : 0;
		return cnt;
	}

	int TorsoBlast(const G3Hit &h, int *out, int *exitz)
	{
		int side = Side(h);
		int n = 0;
		if (side == 1) {
			out[n++] = 8; out[n++] = 6; out[n++] = 4; out[n++] = 14;
			*exitz = 7;
		} else if (side >= 2) {
			out[n++] = 7;
			if (side == 2) {
				out[n++] = 9; out[n++] = 14; out[n++] = 4;
			} else {
				out[n++] = 10; out[n++] = 14; out[n++] = 6;
			}
			out[n++] = 13;
			*exitz = 8;
		} else {
			out[n++] = 7; out[n++] = 5; out[n++] = 3; out[n++] = 13;
			*exitz = 8;
		}
		return n;
	}

	// zone of a hit record: only through the zones' owner parts (SPEC-AMEND-005 G3.3; records on caps, wound meshes
	// and other parts belong to no zone)
	int RecordZone(const HitRecord &r, bool knifeDM)
	{
		if (!gore || !r.part)
			return -1;
		vec3_t p = {r.point[0], r.point[1], r.point[2]};
		return gore->ZoneByOwnerPart(r.part, p, knifeDM);
	}

	// 10.2 (with SPEC-AMEND-005 G3.3, G3.4, G3.6, G3.12): returns the number of hit zones (0 or 1);
	// *done = the hit does nothing
	int Traced(G3Hit &h, int *zones, int *exitz, vec3_t bloodPoint, bool *haveBlood, bool thrownKnife, bool *done)
	{
		*done = false;
		*exitz = -1;
		*haveBlood = false;
		IGhoulInst *inst = Inst();
		if (dmg_shot.ent != ent || dmg_shot.count == 0) {
			*done = true;
			return 0;
		}
		int n = dmg_shot.count;
		bool knifeDM = thrownKnife && G3_Deathmatch();
		// players: a hit whose only record is on `_mohawk` does nothing (10.2)
		if (ent->client && n == 1 && dmg_shot.rec[0].inst == inst) {
			GhoulID mohawk = G3G_Part(inst, "_mohawk");
			if (mohawk && dmg_shot.rec[0].part == mohawk) {
				*done = true;
				return 0;
			}
		}
		// exit: the farthest body record that maps to a zone (thrown knives have no exit)
		int exitRec = -1;
		if (!thrownKnife)
			for (int i = n - 1; i >= 0; i--) {
				const HitRecord &r = dmg_shot.rec[i];
				if (r.inst != inst || !r.part)
					continue;
				int z = RecordZone(r, knifeDM);
				if (z >= 0) {
					exitRec = i;
					*exitz = z;
					break;
				}
			}
		// first body record
		int firstBody = -1;
		for (int i = 0; i < n; i++)
			if (dmg_shot.rec[i].inst == inst && dmg_shot.rec[i].part) {
				firstBody = i;
				break;
			}
		bool boltFirst = false;
		if (dmg_shot.count > 0 && dmg_shot.rec[0].inst != inst &&
		    (firstBody < 0 || dmg_shot.rec[0].distance < dmg_shot.rec[firstBody].distance))
			boltFirst = !retracing; // the masked-head re-trace does not treat a bolt-on a second time
		if (boltFirst) {
			IGhoulInst *b = dmg_shot.rec[0].inst;
			const char *name = G3G_ModelName(b ? b->GetGhoulObject() : NULL);
			GhoulID bolt = 0;
			if (b && b->GetParent() == inst) {
				int k = inst->NumChildren();
				for (int c = 0; c < k; c++) {
					GhoulID bb;
					if (inst->GetChild(c, &bb) == b)
						bolt = bb;
				}
			}
			bool flagged = (ent->flags & 0x40000) != 0; // entity flag 0x40000: neither b) nor c) applies (G3.12)
			if (!Q_stricmp(name, "acc_tanks")) {
				if (gore)
					gore->FuelTankExplode(b, h);
			} else if (!flagged && HandBolt(bolt) >= 0 && Body_WeaponNumber(name) > 0 && !G3_Deathmatch() &&
			           CalmWeapon()) {
				// b) a calm character's held weapon: the hit continues with no zone (G3.12)
				if (h.damage < 4) {
					*done = true;
					return 0;
				}
				return 0;
			} else if (!flagged && gore && b) {
				int hand = HandBolt(bolt);
				int before = rightType;
				h.damage = gore->KnockOff(b, h.damage, h, false);
				if (hand == 0 && before && !rightType)
					disarmed = true;
				if (h.damage <= 0) {
					*done = true; // the bolt-on absorbed the hit
					return 0;
				}
			}
		}
		haveExitPoint = exitRec >= 0;
		if (haveExitPoint)
			VectorMA(dmg_shot.worldStart, dmg_shot.rec[exitRec].distance, dmg_shot.worldDir, exitPoint);
		// entry: the first zone-mapping body record before the exit record (G3.4); thrown knives: every record but
		// the last
		int entryRec = -1, entryZone = -1;
		int limit = thrownKnife ? n - 1 : (exitRec >= 0 ? exitRec : n);
		for (int i = 0; i < limit; i++) {
			const HitRecord &r = dmg_shot.rec[i];
			if (r.inst != inst || !r.part)
				continue;
			int z = RecordZone(r, knifeDM);
			if (z >= 0) {
				entryRec = i;
				entryZone = z;
				break;
			}
		}
		if (entryRec < 0) {
			// a bolt-on hit first with no zone before the exit record: nothing. Otherwise the hit continues with no
			// hit zone (G3.3, G3.4); the exit zone (if any) stays
			if (boltFirst) {
				*done = true;
				*exitz = -1;
			}
			return 0;
		}
		// players (SPEC-720 10.2): a head hit whose zone owner is the mask or hood head is traced again with the
		// bare head (mask hidden, `_baldhead` and `_comface` shown and owning the head zones) and
		// resolved; then the parts are restored and the zones' owner points to the mask again
		if (ent->client && !retracing && gore && (entryZone == GZ_HEAD_FRONT || entryZone == GZ_HEAD_BACK)) {
			GhoulID own = gore->zones[entryZone].owner;
			GhoulID mask = G3G_Part(inst, "_maskhead"), hood = G3G_Part(inst, "_hoodhead");
			GhoulID bald = G3G_Part(inst, "_baldhead"), face = G3G_Part(inst, "_comface");
			if (own && (own == mask || own == hood) && bald) {
				bool baldOn = G3G_PartShown(inst, bald), faceOn = G3G_PartShown(inst, face);
				G3G_ShowPart(inst, own, false);
				G3G_ShowPart(inst, bald, true);
				G3G_ShowPart(inst, face, true);
				// bare-head owners (SPEC-721 male variant table): front = `_comface`, back = `_baldhead`
				bool rep[2] = {false, false};
				for (int z = GZ_HEAD_FRONT; z <= GZ_HEAD_BACK; z++)
					if (gore->zones[z].owner == own) {
						gore->zones[z].owner = (z == GZ_HEAD_FRONT && face) ? face : bald;
						rep[z] = true;
					}
				vec3_t ws, wd;
				VectorCopy(dmg_shot.worldStart, ws);
				VectorCopy(dmg_shot.worldDir, wd);
				if (Dmg_ShotFill(ent, ws, wd, 8192) <= 0) {
					dmg_shot.ent = ent;
					dmg_shot.count = 0;
				}
				retracing = true;
				int r = Traced(h, zones, exitz, bloodPoint, haveBlood, thrownKnife, done);
				retracing = false;
				G3G_ShowPart(inst, own, true);
				G3G_ShowPart(inst, bald, baldOn);
				G3G_ShowPart(inst, face, faceOn);
				for (int z = GZ_HEAD_FRONT; z <= GZ_HEAD_BACK; z++)
					if (rep[z])
						gore->zones[z].owner = own;
				return r;
			}
		}
		// blood point: the damage point clamped into the target's absolute box (G3.6)
		vec3_t p;
		VectorCopy(h.point, p);
		for (int i = 0; i < 3; i++) {
			float lo = ent->absmin[i], hi = ent->absmax[i];
			if (p[i] < lo)
				p[i] = lo;
			if (p[i] > hi)
				p[i] = hi;
		}
		VectorCopy(p, bloodPoint);
		*haveBlood = true;
		zones[0] = entryZone;
		return 1;
	}

	// calm-weapon test of SPEC-720 10.2 b (G3.12): combat-profile flag 1, or armour percentage > 0.5 and health above
	// half the maximum
	bool CalmWeapon(void)
	{
		if (owner && owner->CombatProfileFlag1())
			return true;
		return ArmourPercentF() > 0.5f && ent->health > 0.5f * ent->max_health;
	}
	vec3_t exitPoint;
	bool haveExitPoint = false;
	bool retracing = false;      // inside the masked-head re-trace
	bool disarmed = false;

	// ---------------------------------------------------------------------------------------------------
	// damage on human bodies (SPEC-720 11, 12 armoured boss)
	// ---------------------------------------------------------------------------------------------------
	void Sparks(const vec3_t at, const G3Hit &h)
	{
		const char *e = h.armorPiercing != 0 ? "weakspark" : (h.armorBypass != 0 ? "armorhit" : "spark2");
		vec3_t d;
		VectorNegate(h.dir, d);
		G3FX_At(e, at, d);
		G3_SoundAt(at, CHAN_AUTO, "impact/armor/hit.wav", 0.6f, ATTN_NORM);
	}

	bool IsArmourCovered(int z) { return (z >= 3 && z <= 8) || z == 24 || z == 25; }
	bool IsLimb(int z) { return (z >= 9 && z <= 12) || (z >= 16 && z <= 23); }

	int Damage(G3Hit &h) override
	{
		edict_t *t = ent;
		int H = t->health;
		bool boss = family == BODY_MESO_ARMOURED;
		int dflags = h.dflags;
		dmg_knifeDeflected = false;
		disarmed = false;
		if (boss) {
			if (h.attacker == t)
				return 0;
			if (firstHitTrigger) {
				firstHitTrigger = false;
				if (t->killtarget)
					G_UseTargets(t, h.attacker);
			}
		}
		// 11.1 forced and blocked
		bool forced = false;
		if (scriptedKill) {
			h.damage = 9999;
			if (scriptedKill == 2)
				dflags |= DMGF_SPREAD;
			scriptedKill = 0;
			forced = true;
		}
		if ((dflags & DMGF_0x800000) && gore && gore->armourType == 3)
			return 0;
		if (boss && !forced && h.damage > 10)
			h.damage = 10;
		bool thrownKnife = h.inflictor && ((h.inflictor->g3flags & G3F_THROWN_KNIFE) ||
		                                   ((h.mod & ~DMG_MOD_FRIENDLY) == MOD_KNIFE_THROWN && (dflags & DMGF_THROWN))) &&
		                   h.attacker && h.attacker->client;
		bool locate = (dflags & (DMGF_TRACED | DMGF_PROJECTILE | DMGF_EXPLOSIVE | DMGF_SPREAD | DMGF_TORSO_BLAST)) ||
		              thrownKnife;
		if (!locate) {
			if (dflags & DMGF_FALL)
				GeneralVoice("Hit", 3, 0.8f);
			if (t->client && Dmg_RealisticDamage() && (dflags & DMGF_FALL)) {
				t->client->legslow += 0.1f;
				if (t->client->legslow > 0.5f)
					t->client->legslow = 0.5f;
			}
			if (boss && !forced) {
				if (h.damage <= 0)
					return h.damage;
				if (h.damage < 5)
					return G3_irand(0, 1) ? 1 : 0;
				return 1;
			}
			return h.damage;
		}
		int zones[8];
		int nz = 0, exitz = -1;
		haveExitPoint = false;
		vec3_t blood;
		bool haveBlood = false;
		bool estimated = false;
		if (!thrownKnife)
			Dmg_EnsureShot(t, h);
		bool noGhoul = dmg_shot.ent != t || dmg_shot.count == 0;
		if (thrownKnife) {
			// 10.3: trace from the attacker's eye along twice the knife velocity
			vec3_t eye, end;
			VectorCopy(h.attacker->s.origin, eye);
			eye[2] += h.attacker->viewheight;
			VectorMA(eye, 2.0f, h.inflictor->velocity, end);
			Dmg_ShotTrace(eye, NULL, NULL, end, h.attacker, 0x6000081);
			noGhoul = dmg_shot.ent != t || dmg_shot.count == 0;
			dflags |= DMGF_PROJECTILE;
		}
		if ((dflags & DMGF_SPREAD) && !(dflags & DMGF_PROJECTILE)) {
			if (hintZone >= 0) {
				nz = SpreadFromHint(hintZone, h, zones);
				hintZone = -1;
			} else {
				nz = Estimate(h, zones, &exitz);
				estimated = true;
			}
		} else if ((dflags & DMGF_SLASH) && noGhoul) {
			if (hintZone >= 0) {
				nz = SpreadFromHint(hintZone, h, zones);
				hintZone = -1;
			} else {
				nz = Estimate(h, zones, &exitz);
				estimated = true;
			}
		} else if (dflags & DMGF_EXPLOSIVE) {
			if (dflags & DMGF_TORSO_BLAST)
				nz = TorsoBlast(h, zones, &exitz);
		} else if (dflags & DMGF_TORSO_BLAST)
			nz = TorsoBlast(h, zones, &exitz);
		else {
			if (thrownKnife && noGhoul) {
				int best = -1;
				float bd = 0;
				for (int i = 0; gore && i < gore->numZones; i++) {
					vec3_t bp, d;
					if (!gore->zones[i].goreBolt || !G3G_BoltWorld(t, Inst(), gore->zones[i].goreBolt, bp))
						continue;
					VectorSubtract(bp, h.inflictor->s.origin, d);
					float dd = DotProduct(d, d);
					if (best < 0 || dd < bd) {
						best = i;
						bd = dd;
					}
				}
				if (best < 0)
					return 0;
				zones[nz++] = best;
			} else {
				bool done;
				nz = Traced(h, zones, &exitz, blood, &haveBlood, thrownKnife, &done);
				if (done)
					return 0;
			}
			if (thrownKnife && nz > 0 && G3_Deathmatch() && G3_irand(0, 1)) {
				if (zones[0] == 0)
					zones[0] = 3;
				else if (zones[0] == 1)
					zones[0] = 4;
				else if (zones[0] == 2)
					zones[0] = FromBehind(h) ? 8 : 7;
			}
		}
		if (nz > 0)
			hintZone = zones[nz - 1];
		else if (!estimated && !(dflags & DMGF_EXPLOSIVE) && !(dflags & DMGF_TORSO_BLAST))
			hintZone = -1; // a traced hit that resolves no zone leaves no hint (G3.4)
		if (G3_Cvar("g3_debug") != 0) { // OpenMerc diagnostic
			char zs[64] = "";
			for (int i = 0; i < nz; i++)
				Com_sprintf(zs + strlen(zs), sizeof zs - strlen(zs), " %d", zones[i]);
			G3_Printf("G3 zones%s exit %d blood %s ghoul hits %d\n", zs, exitz, haveBlood ? G3_vs(blood) : "-",
			          dmg_shot.ent == t ? dmg_shot.count : 0);
		}
		if (G3_GoreTest()) {
			G3_Printf("%d hits!\n", dmg_shot.ent == t ? dmg_shot.count : 0);
			if (haveBlood)
				G3_Printf("Entry wound position: %s\n", vtos(blood));
		}
		// 11.2 slash
		if (dflags & DMGF_SLASH) {
			vec3_t f, l, u, d;
			G3_EntityAxes(t, f, l, u);
			VectorSubtract(t->s.origin, h.origin, d);
			VectorNormalize(d);
			if (DotProduct(f, d) > 0.707f)
				h.damage = (int)(h.damage * 2.5f);
			if (estimated)
				for (int i = 0; i < nz; i++)
					if (zones[i] <= 2 || zones[i] >= 20)
						zones[i] = 8;
		}
		for (int i = 0; i < nz; i++)
			if (zones[i] >= 9 && zones[i] <= 12)
				armHitPending = true;
		int damage = h.damage;
		// 11.3 players
		if (t->client) {
			gclient_t *cl = t->client;
			bool absorbed = false;
			if (!(dflags & DMGF_NO_ARMOR) && cl->inv) {
				int before = damage;
				if (Dmg_RealisticDamage() && nz > 0) {
					int share = damage / nz, left = 0;
					for (int i = 0; i < nz; i++)
						left += IsArmourCovered(zones[i]) ? Dmg_AbsorbArmour(cl->inv, share, h.armorPiercing,
						                                                     h.armorBypass)
						                                  : share;
					damage = left;
				} else
					damage = Dmg_AbsorbArmour(cl->inv, damage, h.armorPiercing, h.armorBypass);
				if (damage < 0)
					damage = 0;
				absorbed = damage != before;
			}
			(void)absorbed;
			if (damage == 0 && h.damage > 0) {
				Sparks(haveBlood ? blood : h.point, h);
				if (thrownKnife)
					dmg_knifeDeflected = true;
				return 0;
			}
		} else if (!(dflags & DMGF_EXPLOSIVE) && gore) {
			// 11.4 character zone armour
			for (int i = 0; i < nz; i++) {
				G3Zone &z = gore->zones[zones[i]];
				if (z.damage >= 0)
					continue;
				IGhoulInst *inst = Inst();
				GhoulID arm = G3G_Skin(inst, gore->gzMaterial, "gz_armor");
				if (z.woundMesh && arm) {
					inst->SetSkin(gore->gzMaterial, arm, z.woundMesh);
					inst->SetPartOnOff(z.woundMesh, true);
				}
				if (z.link >= 0 && gore->zones[z.link].woundMesh && arm) {
					inst->SetSkin(gore->gzMaterial, arm, gore->zones[z.link].woundMesh);
					inst->SetPartOnOff(gore->zones[z.link].woundMesh, true);
				}
				int d = (int)(damage - damage * h.armorBypass);
				if (h.armorPiercing > 0)
					d *= 2;
				int remaining = -z.damage;
				if (d <= remaining) {
					z.damage += d;
					armHitPending = false;
					if (haveBlood)
						Sparks(blood, h);
					if (thrownKnife)
						dmg_knifeDeflected = true;
					return 0;
				}
				z.damage = 0;
				damage = d - remaining;
				if (h.armorPiercing > 0)
					damage /= 2;
			}
		}
		// 11.5 hit effects (SPEC-AMEND-005 G3.7, G3.6): the per-hit debris puff — TE 18 at the damage point, direction
		// (0,0,0), irand(0, 5) chunks (0 is sent too) of size class 4, flesh (32), no extents, material 35, through the
		// game-wide TE 18 filter — then the blood splat (TE 3) from the blood point on every hit that has one
		if (h.point[0] != 0 || h.point[1] != 0 || h.point[2] != 0)
			G3FX_Debris(h.point, vec3_origin, G3_irand(0, 5), 4, 32, vec3_origin, 35);
		if (haveBlood)
			G3FX_BloodSplat(blood, h.dir, t);
		// 11.6 zone multiplier
		if (!(dflags & DMGF_EXPLOSIVE) && nz > 0 && gore) {
			int sum = 0;
			for (int i = 0; i < nz; i++) {
				const G3Zone &z = gore->zones[zones[i]];
				int di;
				if (H > 0) {
					di = (int)(damage * z.scale);
					if (Dmg_RealisticDamage() && IsLimb(zones[i])) {
						int cap = (int)(H * 0.33f);
						if (di > cap)
							di = cap;
					}
				} else
					di = damage > 0 ? (z.bodyCap ? 3 : 12) : 0;
				sum += di;
			}
			if (!(H <= 0 && damage <= 0))
				damage = sum / nz;
		}
		// 11.7 overkill cap
		if (H > 0 && damage > H + 150)
			damage = H + 150;
		// 11.8 reactions
		bool killing = H > 0 && H <= damage;
		if (H > 0)
			SetExpression(EXPR_PAIN, killing ? 99999.0f : 10.0f, false);
		lastHitTime = G3_Time();
		lastHitFlags = dflags;
		if (killing) {
			IGhoulInst *inst = Inst();
			if (AllowsDrops() && inst && gore) {
				const char *bolts[3] = {"wbolt_hand_l", "wbolt_hand_r", "abolt_head_t"};
				for (int b = 0; b < 3; b++) {
					GhoulID id = G3G_Part(inst, bolts[b]);
					for (int k = 0; k < 10; k++) {
						IGhoulInst *c = G3G_ChildOnBolt(inst, id);
						if (!c)
							break;
						int nb = G3G_NumChildren(inst);
						gore->KnockOff(c, damage, h, false);
						if (G3G_NumChildren(inst) == nb)
							break;
					}
				}
			}
		}
		bool gib = ((dflags & DMGF_EXPLOSIVE) && H - damage < -50) || H - damage < -750;
		if (gib && gore) {
			gore->Gib(h);
			bool boss2 = G3_ClassIs(t, "m_x_mskinboss") || G3_ClassIs(t, "m_x_mraiderboss1");
			if ((dflags & DMGF_FIRE) && !g3locks.deaths && !boss2 && g3hooks.setOnFire)
				g3hooks.setOnFire(t, h.attacker, 2.0f);
		}
		// 11.9 limb wounds
		for (int i = 0; i < nz; i++) {
			int z = zones[i], limb = -1;
			if (z == 16 || z == 17 || z == 20 || z == 22)
				limb = 0;
			else if (z == 18 || z == 19 || z == 21 || z == 23)
				limb = 1;
			else if (z == 9 || z == 11)
				limb = 2;
			else if (z == 10 || z == 12)
				limb = 3;
			if (limb < 0)
				continue;
			limbExpire[limb] = G3_Time() + G3_flrand(5, 7) * (4 - Dmg_Toughness()) * 0.25f;
			if (limb <= 1 && t->client && Dmg_RealisticDamage()) {
				t->client->legslow += 0.1f;
				if (t->client->legslow > 0.5f)
					t->client->legslow = 0.5f;
			}
		}
		// 11.10 wounds, severing. Also after a full gib in this hit: the original then still wounds the zones (wound
		// meshes, killing-blow clouds, exit wound with its bolt-on) — observed black box, DM self-rocket.
		if (gore) {
			bool armouredPlayer = t->client && Dmg_RealisticDamage() && t->client->inv && t->client->inv->Armor() > 0;
			if (nz > 0)
				gore->NoteKillingHit(zones[0], damage, H); // blood pool + killing zone (G3.1)
			for (int i = 0; i < nz; i++) {
				int z = zones[i];
				gore->lastZone = z;
				if (armouredPlayer && IsArmourCovered(z))
					continue;
				bool kill = gore->WoundZone(z, damage, H, h.mod, dflags, h.attacker, h.dir);
				bool severed = false;
				if (kill && gore->CanSever(z, damage, H, dflags)) {
					if (gore->zones[z].segment == SEG_HEAD)
						gore->HeadExplosion(z, haveBlood ? blood : h.point);
					else
						gore->Sever(z, exitz, false, vec3_origin, false);
					severed = true;
				}
				gore->KillingCloud(z, damage, H); // after the zone's wound / sever / head explosion (G3.1)
				if (severed)
					break;
			}
			// exit wound; without an entry zone a living target gets nothing, a corpse rule 5.2 (G3.4)
			if (exitz >= 0)
				gore->ExitWound(nz > 0 ? zones[0] : -1, exitz, damage, H, dflags, haveExitPoint ? exitPoint : h.point,
				                h.dir);
			for (int i = 0; i < nz; i++)
				if (zones[i] <= GZ_NECK)
					gore->voiceDisabled = true;
			if (exitz >= 0 && exitz <= GZ_NECK)
				gore->voiceDisabled = true;
			if (thrownKnife && nz > 0 && !t->client)
				gore->StickKnife(zones[0], h);
		}
		return damage;
	}

	// ---------------------------------------------------------------------------------------------------
	// choices (SPEC-722)
	// ---------------------------------------------------------------------------------------------------
	const G3Anim *ReactionOverride(void)
	{
		float blind = owner ? owner->FlashBlindness() : 0;
		bool inBlind = cur && cur->group == 2;
		if (blind >= 20) {
			if (!inBlind) {
				const G3Anim *a = Avail("std_pblinded_to_a_n");
				if (a)
					return a;
			}
			const G3Anim *a = Avail("std_pblinded_loop_a_n");
			if (a)
				return a;
		} else if (inBlind && cur != Anim_Human("std_pblinded_frm_a_n")) {
			const G3Anim *a = Avail("std_pblinded_frm_a_n");
			if (a)
				return a;
		}
		if (Posture() == POSTURE_STAND && Dmg_Toughness() <= 3) {
			const char *n = NULL;
			if (LimbActive(0))
				n = "std_phop_lt_n_n";
			else if (LimbActive(1))
				n = "std_phop_rt_n_n";
			else if (LimbActive(2))
				n = "std_parm_rt_n_n";
			else if (LimbActive(3))
				n = "std_parm_lt_n_n";
			if (n) {
				const G3Anim *a = Avail(n);
				if (a) {
					if (a != cur)
						GeneralVoice("Writhe", 7, 0.8f);
					return a;
				}
			}
		}
		if (cur && cur->group == 3 && Posture() == POSTURE_STAND && owner && owner->JustNoticedEnemy()) {
			owner->OnStartled();
			if (G3_Time() - wakeTime >= 7.0f)
				Wake(0.9f);
			const G3Anim *a = Avail("std_xstartled_n_a_a");
			if (a)
				return a;
		}
		if (Armed() && Posture() == POSTURE_CROUCH && clip <= 0) {
			/* crouched with an empty clip: the crouched reload (cch_l_fwd_<w>) when the model has it. Without this a
			 * crouched shooter never reloaded and fired without end (g4jpn3d ninja: a 90-shot burst; the original one
			 * reloads between bursts of about 33, the type-5 clip) */
			char nm[64];
			Com_sprintf(nm, sizeof nm, "cch_l_fwd_%s", Tok(WClass(), NULL, "ms_2", "r_2", "l_2", "p_2", "p_2"));
			if (Avail(nm)) {
				G3MoveInput in;
				memset(&in, 0, sizeof in);
				in.posture = -1;
				const G3Anim *a = ChooseReload(in);
				if (a)
					return a;
			} else if (!(ent->spawnflags & 0x80)) {
				/* no crouched reload in the model (the ninja's p_2 set): stand up first, the standing reload follows */
				const G3Anim *a = ChoosePosture(POSTURE_STAND, 0);
				if (a)
					return a;
			}
		}
		if (Armed() && Posture() != POSTURE_CROUCH) {
			if (clip <= 0) {
				G3MoveInput in;
				memset(&in, 0, sizeof in);
				in.posture = -1;
				const G3Anim *a = ChooseReload(in);
				if (a)
					return a;
			}
			if (justShot) {
				justShot = false;
				const G3Anim *a = ChooseCock();
				if (a)
					return a;
			}
		}
		if (cur == Anim_Human("std_xstartled_n_a_a") && AnimDone() && Armed() && G3_irand(0, 1) == 0) {
			G3MoveInput in;
			memset(&in, 0, sizeof in);
			in.posture = -1;
			return ChooseReload(in);
		}
		return NULL;
	}

	const G3Anim *PreEmption(const G3MoveInput &in, bool *stop)
	{
		*stop = true;
		if (in.forced && HasAnim(in.forced))
			return in.forced;
		const G3Anim *r = ReactionOverride();
		if (r)
			return r;
		if (in.posture >= 0 && in.posture != Posture()) {
			const G3Anim *p = ChoosePosture(in.posture, in.speed);
			if (p)
				return p;
		}
		if (owner && Posture() == POSTURE_STAND) {
			if (LimbActive(0)) {
				const G3Anim *a = Avail("std_plimp_lt_n_n");
				if (a)
					return a;
			} else if (LimbActive(1)) {
				const G3Anim *a = Avail("std_plimp_rt_n_n");
				if (a)
					return a;
			}
		}
		if (Posture() == POSTURE_PRONE || (cur && cur->cls == ANIMCLS_FALL && cur->posture == POSTURE_PRONE)) {
			const G3Anim *a = Avail("prn_a_n_mrs_2");
			if (a)
				return a;
		}
		if (Posture() == POSTURE_ONSIDE && cur)
			return cur;
		*stop = false;
		return NULL;
	}

	const G3Anim *ChooseMove(const G3MoveInput &in) override
	{
		if (ent->spawnflags & 0x80)
			return ChooseStand(in);
		bool stop;
		const G3Anim *a = PreEmption(in, &stop);
		if (stop)
			return a;
		SetSpeed4();
		int wc = WClass();
		vec3_t f, l, u;
		G3_EntityAxes(ent, f, l, u);
		float dotMove = DotProduct(in.moveDir, f);
		float dotTarget = 1;
		if (in.target) {
			vec3_t d;
			VectorSubtract(in.target->s.origin, ent->s.origin, d);
			VectorNormalize(d);
			dotTarget = DotProduct(in.moveDir, d);
		}
		if (Posture() == POSTURE_CROUCH) {
			if (in.target && dotTarget <= 0)
				return First({(wc & (WC_M | WC_R | WC_S)) ? "cch_raim_fwd_mrs_2" : NULL,
					(wc & WC_L) ? "cch_raim_n_l_2" : NULL, (wc & WC_K) ? "cch_r_n_pk_n" : NULL, "cch_raim_fwd_p_2"});
			a = First({(wc & (WC_M | WC_R | WC_S)) ? "cch_raim_fwd_mrs_2" : NULL,
				(wc & WC_L) ? "cch_raim_n_l_2" : NULL, (wc & WC_K) ? "cch_r_n_pk_n" : NULL, "cch_raim_fwd_p_2",
				(wc & WC_R) ? "cch_a_fwd_r_2" : NULL, (wc & (WC_M | WC_S)) ? "cch_a_fwd_ms_2" : NULL,
				(wc & WC_L) ? "cch_a_fwd_l_2" : NULL, (wc & (WC_P | WC_MP)) ? "cch_a_fwd_p_2" : NULL});
			if (a)
				return a;
		}
		if (in.speed * (dotMove > 0 ? dotMove : -dotMove) < 10 && in.speed < 10) {
			const char *tok = (wc & (WC_M | WC_R | WC_S)) ? "mrs_2" : "pk_n";
			char nm[64];
			if (in.speed < 0.1f && in.turn > 0 && in.turn < 10) {
				a = Avail("std_wturn180_lt_mrs_2");
				if (a)
					return a;
			}
			if (in.turn > 1 || in.turn < -1) {
				Com_sprintf(nm, sizeof nm, "std_wturn90_%s_%s", in.turn > 0 ? "lt" : "rt", tok);
				a = Avail(nm);
				if (a)
					return a;
			}
			return ChooseStand(in);
		}
		if (dotMove < -0.75f && !in.running) {
			if (!Armed())
				return First({"std_wnormal_n_pk_a"});
			return First({(wc & WC_R) ? "std_wbackaim_n_r_2" : NULL, (wc & (WC_M | WC_S)) ? "std_wbackaim_n_ms_2" : NULL,
				(wc & WC_L) ? "std_wbackaim_n_l_2" : NULL, "std_wbackaim_n_p_2", "std_wnormal_n_pk_a"});
		}
		bool right = DotProduct(in.moveDir, l) < 0;
		if (in.running) {
			if (dotMove < -0.75f)
				return First({(wc & (WC_M | WC_R | WC_S)) ? "std_rbackaim_n_mrs_2" : NULL,
					(wc & (WC_P | WC_MP)) ? "std_rbackaim_n_p_2" : NULL, (wc & WC_L) ? "std_rbackaim_n_l_2" : NULL,
					"std_r_n_pk_n"});
			if (in.aiming && in.strafing)
				return First({(wc & (WC_M | WC_R | WC_S)) ? (right ? "std_raimstraf_rt_mrs_2" : "std_raimstraf_lt_mrs_2")
				                                          : NULL,
					(wc & WC_L) ? (right ? "std_raimstraf_rt_l_2" : "std_raimstraf_lt_l_2") : NULL,
					right ? "std_raimstraf_rt_p_cbn" : "std_raimstraf_lt_p_cbn", "std_r_n_pk_n"});
			if (!Armed() && expr == EXPR_AFRAID) {
				a = Avail("std_rafraid_n_n_n");
				if (a)
					return a;
			}
			if (in.aiming)
				return First({(wc & (WC_M | WC_R | WC_S)) ? "std_raim_n_mrs_2" : NULL,
					(wc & (WC_P | WC_MP)) ? "std_raim_n_p_2" : NULL, (wc & (WC_P | WC_MP | WC_K)) ? "std_raim_n_p_n" : NULL,
					(wc & WC_L) ? "std_raim_n_l_2" : NULL, "std_r_n_pk_n"});
			return First({(wc & (WC_M | WC_R | WC_S)) ? "std_r_n_mrs_2" : NULL,
				(wc & (WC_P | WC_MP)) ? "std_rgunup_n_p_2" : NULL, "std_r_n_pk_n"});
		}
		if (in.aiming && in.strafing)
			return First({(wc & (WC_M | WC_S)) ? (right ? "std_waimstraf_rt_ms_2" : "std_waimstraf_lt_ms_2") : NULL,
				(wc & WC_R) ? (right ? "std_waimstraf_rt_r_2" : "std_waimstraf_lt_r_2") : NULL,
				(wc & WC_L) ? (right ? "std_waimstraf_rt_l_2" : "std_waimstraf_lt_l_2") : NULL,
				right ? "std_waimstraf_rt_p_2" : "std_waimstraf_lt_p_2", "std_wnormal_n_pk_a"});
		if (in.aiming)
			return First({(wc & (WC_P | WC_MP | WC_K)) ? "std_waim_fwd_p_2" : NULL, (wc & WC_R) ? "std_waim_fwd_r_2" : NULL,
				(wc & (WC_M | WC_S)) ? "std_waim_fwd_ms_2" : NULL, (wc & WC_L) ? "std_waim_fwd_l_2" : NULL,
				"std_wnormal_n_pk_a"});
		return First({(wc & (WC_M | WC_R | WC_S)) ? "std_wpatrol_n_mrs_2" : NULL, (wc & WC_L) ? "std_w_n_l_2" : NULL,
			Armed() ? NULL : "std_wattitude_n_pk_a", "std_wnormal_n_pk_a"});
	}

	void SetSpeed4(void)
	{
		IGhoulInst *inst = Inst();
		if (inst)
			inst->SetSpeed(4);
	}

	const G3Anim *ChooseStand(const G3MoveInput &in) override
	{
		bool stop;
		G3MoveInput in2 = in;
		const G3Anim *a = PreEmption(in2, &stop);
		if (stop)
			return a;
		SetSpeed4();
		int wc = WClass();
		if (expr == EXPR_AFRAID) {
			if (Posture() == POSTURE_CROUCH)
				return First({G3_irand(0, 1) ? "cch_xcchcower_mid_a_a" : "cch_xcchcower2_mid_a_a",
					"cch_xcchcower_mid_a_a"});
			if (G3_irand(0, 10) == 0) {
				a = Avail("std_xfearwave_n_n_n");
				if (a)
					return a;
			}
			a = Avail("std_xafraidshake_n_n_n");
			if (a)
				return a;
		}
		if (Posture() == POSTURE_ONBACK && ent->health > 0)
			return First({"lb_pcrawl_bk_p_n", "lb_xleanupshoot_n_p_n", "lb_sautofire_end_n_n"});
		if (in.target && in.aiming) {
			vec3_t d;
			VectorSubtract(in.aimPoint, ent->s.origin, d);
			float horiz = sqrtf(d[0] * d[0] + d[1] * d[1]);
			const char *t2 = Tok(wc, "mrs_2", NULL, NULL, "l_2", "p_2", "p_2");
			char nm[64];
			if (d[2] > horiz * 0.5f || d[2] < -horiz * 0.5f) {
				Com_sprintf(nm, sizeof nm, "std_a_%s_%s", d[2] > 0 ? "up" : "dwn", t2);
				a = Avail(nm);
				if (a)
					return a;
			}
			if (cur && cur->cls == ANIMCLS_ATTACK && G3_irand(0, 14) != 0) {
				a = First({(wc & (WC_M | WC_R | WC_S)) ? (G3_irand(0, 1) ? "std_xatkpause1_n_mrs_2"
				                                                          : "std_xatkpause2_n_mrs_2")
				                                       : NULL,
					(wc & (WC_P | WC_K)) ? "std_xatkpause1_n_pk_n" : NULL});
				if (a)
					return a;
			}
			return First({(wc & (WC_M | WC_S)) ? "std_a_fwd_ms_2" : NULL, (wc & WC_R) ? "std_a_fwd_r_2" : NULL,
				(wc & (WC_P | WC_MP)) ? "std_a_fwd_p_2" : NULL, (wc & (WC_P | WC_MP)) ? "std_a_fwd_p_cb" : NULL,
				(wc & WC_L) ? "std_a_fwd_l_2" : NULL, "std_istand_n_a_n"});
		}
		if (in.target) {
			if (wc & (WC_M | WC_R | WC_S)) {
				if (G3_irand(0, 12) == 0 && (a = Avail("std_ialertlk_rt_mrs_2")))
					return a;
				if (G3_irand(0, 10) == 0 && (a = Avail("std_ialerttrn_lt_mrs_2")))
					return a;
				if ((a = Avail("std_ialert_n_mrs_2")))
					return a;
			} else if (wc & (WC_P | WC_K | WC_MP)) {
				if (G3_irand(0, 14) == 0 && (a = Avail("std_ialertlk_lt_p_n")))
					return a;
				if (G3_irand(0, 8) == 0 && (a = Avail("std_ialerttrn_rt_p_n")))
					return a;
				if ((a = Avail("std_ialert_n_p_n")))
					return a;
			}
		}
		// idle: keep a running fidget, never repeat one back to back
		if (cur && cur->cls == ANIMCLS_STATIONARY && !AnimDone())
			return cur;
		int counter = (int)(G3_Time() * 10);
		if (wc & (WC_M | WC_R | WC_S)) {
			if (counter % 5 == 0 && lastFidget != 1 && (a = Avail("std_ilooking_n_mrs_2"))) {
				lastFidget = 1;
				return a;
			}
			if (counter % 3 == 0 && lastFidget != 2 && (a = Avail("std_istretch_n_mrs_2"))) {
				lastFidget = 2;
				return a;
			}
			lastFidget = 0;
			if ((a = Avail("std_istand_n_mrs_2")))
				return a;
		}
		if (counter % 6 == 0 && lastFidget != 3 && (a = Avail("std_ilooking_n_a_a"))) {
			lastFidget = 3;
			return a;
		}
		if (counter % 3 == 0 && lastFidget != 4 && (a = Avail("std_istretch_n_a_a"))) {
			lastFidget = 4;
			return a;
		}
		if (counter % 2 == 0 && lastFidget != 5 && (a = Avail("std_ishift_n_a_n"))) {
			lastFidget = 5;
			return a;
		}
		lastFidget = 0;
		return First({"std_istand_n_a_n"});
	}

	const G3Anim *ChooseAttack(const G3MoveInput &in) override
	{
		bool stop;
		const G3Anim *a = PreEmption(in, &stop);
		if (stop)
			return a;
		SetSpeed4();
		int wc = WClass();
		if (in.speed >= 10)
			return ChooseMove(in);
		if (Posture() == POSTURE_CROUCH) {
			a = First({(wc & WC_M) ? "cch_f_fwd_m_2" : NULL, (wc & WC_R) ? "cch_f_fwd_r_2" : NULL,
				(wc & WC_S) ? "cch_f_fwd_s_2" : NULL, (wc & WC_P) ? "cch_f_fwd_p_2" : NULL});
			if (!a && (wc & WC_MP))
				a = AvailFire("cch_a_fwd_p_2");
			if (!a)
				a = First({(wc & WC_L) ? "cch_f_fwd_l_2" : NULL, (wc & WC_K) ? "cch_rswipe_n_k_n" : NULL});
			if (a)
				return a;
			return ChooseMove(in);
		}
		if (in.meleeRange && (wc & (WC_K | WC_P)) && (a = Avail("std_mslash_n_kp_n")))
			return a;
		if ((wc & WC_K) && in.running && (a = Avail("std_rswipe_n_k_n")))
			return a;
		if (in.target) {
			vec3_t d;
			VectorSubtract(in.aimPoint, ent->s.origin, d);
			float horiz = sqrtf(d[0] * d[0] + d[1] * d[1]);
			if (d[2] > horiz * 0.5f || d[2] < -horiz * 0.5f) {
				char nm[64];
				Com_sprintf(nm, sizeof nm, "std_a_%s_%s", d[2] > 0 ? "up" : "dwn",
				            Tok(wc, "mrs_2", NULL, NULL, "l_2", "p_2", "p_2"));
				if ((a = Avail(nm)))
					return a;
			}
		}
		a = First({(wc & WC_M) ? (G3_irand(0, 1) ? "std_fautosweep_n_m_2" : "std_f_fwd_m_2") : NULL,
			(wc & WC_M) ? "std_f_fwd_m_2" : NULL, (wc & WC_R) ? "std_f_fwd_r_2" : NULL,
			(wc & WC_S) ? "std_f_fwd_s_2" : NULL, (wc & WC_L) ? "std_f_fwd_l_2" : NULL,
			(wc & WC_P) ? (G3_irand(0, 9) == 0 ? "std_fattitude_n_p_a" : "std_f_n_p_2") : NULL,
			(wc & WC_P) ? "std_f_fwd_p_2" : NULL, (wc & WC_P) ? "std_f_fwd_p_cb" : NULL});
		if (!a && (wc & WC_MP))
			a = AvailFire("std_a_fwd_p_2");
		return a;
	}

	const G3Anim *ChooseJump(const G3MoveInput &in) override
	{
		if (in.forced && HasAnim(in.forced))
			return in.forced;
		const G3Anim *a = ReactionOverride();
		if (a)
			return a;
		if (in.posture >= 0 && in.posture != Posture() && (a = ChoosePosture(in.posture, in.speed)))
			return a;
		SetSpeed4();
		a = First({(in.highJump && (WClass() & WC_L)) ? "std_jstraitup_up_l_2" : NULL, "std_jstraitup_up_a_a",
			"std_jupto_n_a_a"});
		return a ? a : ChooseStand(in);
	}

	const G3Anim *ChooseDodge(int dir) override
	{
		SetSpeed4();
		switch (dir) {
		case 0: return Avail("std_xroll_lt_a_a");
		case 1: return Avail("std_xroll_rt_a_a");
		case 2: return Avail("std_jbackflip_n_p_n");
		case 3: return Avail("std_xrolltocch_fwd_a_a");
		case 4: return Avail("std_jdive2prone_n_a_n");
		}
		return NULL;
	}

	const G3Anim *ChoosePosture(int posture, float speed) override
	{
		int wc = WClass();
		int curp = Posture();
		if (posture == POSTURE_CROUCH && curp == POSTURE_STAND)
			return First({(wc & (WC_P | WC_MP)) ? "std_xcrouch_dwn_p_a" : NULL,
				Armed() ? NULL : "std_xcchcower_dwn_a_a", speed > 2 ? "std_xrolltocch_fwd_a_a" : NULL,
				(wc & (WC_M | WC_R | WC_S)) ? "std_xcrouch_dwn_mrs_2" : NULL, (wc & WC_L) ? "std_xcrouch_dwn_l_2" : NULL,
				"std_xcrouch_dwn_pk_a"});
		if (posture == POSTURE_PRONE)
			return Avail("std_xgoprone_n_a_a");
		if (posture == POSTURE_STAND && !(ent->spawnflags & 0x80)) {
			if (curp == POSTURE_CROUCH)
				return First({Armed() ? NULL : "cch_xcchcower_up_a_a",
					(wc & (WC_M | WC_R | WC_S)) ? "cch_xcrouch_up_mrs_2" : NULL, (wc & WC_L) ? "cch_xcrouch_up_l_2" : NULL,
					"cch_xcrouch_up_pk_a"});
			if (curp == POSTURE_PRONE)
				return Avail("prn_xtostand_n_a_a");
			if (curp == POSTURE_ONBACK) {
				const G3Anim *a = Avail("lb_xgetup_n_a_n");
				if (a)
					GeneralVoice("Getup", 1, 0.8f);
				return a;
			}
		}
		return NULL;
	}

	const G3Anim *ChooseReload(const G3MoveInput &in) override
	{
		int wc = WClass();
		const char *w = Tok(wc, NULL, "ms_2", "r_2", "l_2", "p_2", "p_2");
		char nm[64];
		const G3Anim *a = NULL;
		vec3_t f, l, u;
		G3_EntityAxes(ent, f, l, u);
		float df = DotProduct(in.moveDir, f), dr = -DotProduct(in.moveDir, l);
		if (Posture() == POSTURE_CROUCH) {
			if (in.speed > 10) {
				Com_sprintf(nm, sizeof nm, "cch_lrun_%s_%s", df < -0.5f ? "bk" : "fwd", w);
				a = Avail(nm);
			}
			if (!a) {
				Com_sprintf(nm, sizeof nm, "cch_l_fwd_%s", w);
				a = Avail(nm);
			}
		} else if (in.speed > 10) {
			const char *kind = in.running ? "lrun" : "lwalk";
			if (df > 0.5f)
				Com_sprintf(nm, sizeof nm, "std_%s_fwd_%s", kind, w);
			else if (df < -0.5f)
				Com_sprintf(nm, sizeof nm, "std_%s_bk_%s", kind, w);
			else
				Com_sprintf(nm, sizeof nm, "std_%sstraf_%s_%s", kind, dr > 0 ? "rt" : "lt", w);
			a = Avail(nm);
		}
		if (!a) {
			Com_sprintf(nm, sizeof nm, "std_l_n_%s", w);
			a = Avail(nm);
		}
		if (!a)
			a = Avail("std_l_n_p_n");
		if (a)
			clip = ClipRounds(rightType);
		return a;
	}

	const G3Anim *ChooseCock(void) override
	{
		if (Posture() == POSTURE_STAND) {
			if (rightType == 11)
				return Avail("std_xcock_n_s_2");
			if (rightType == 7)
				return Avail("std_xcock_n_r_2");
		} else if (Posture() == POSTURE_CROUCH && rightType == 11)
			return Avail("cch_xcock_n_s_2");
		return NULL;
	}

	const G3Anim *ChoosePain(edict_t *attacker, const vec3_t point, int knockback, int damage) override
	{
		(void)attacker;
		(void)knockback;
		if (Posture() != POSTURE_STAND)
			return NULL;
		if (family == BODY_MESO_ARMOURED) {
			if (G3_Time() - lastPainTime < 5.0f)
				return NULL;
			lastPainTime = G3_Time();
		}
		SetSpeed4();
		const G3Anim *a;
		if (armHitPending) {
			armHitPending = false;
			if ((ArmourPercentF() <= 0.5f || ent->health <= ent->max_health / 2) && !(owner && owner->NoDisarm()) &&
			    (a = Avail("std_sgunfromhand_n_n_n"))) {
				GeneralVoice("Disarm", 1, 0.8f);
				DropHandWeapon(0);
				return a;
			}
			if ((a = Avail("std_sarm_rt_n_n")))
				return a;
		}
		if (LimbActive(0)) {
			if ((a = Avail("std_sleg_rt_n_n"))) {
				GeneralVoice("Scream", 8, 0.8f);
				return a;
			}
			if ((a = Avail("std_slegunhurt_rt_a_n")))
				return a;
		}
		if (LimbActive(1)) {
			if ((a = Avail("std_sleg_lt_n_n"))) {
				GeneralVoice("Scream", 8, 0.8f);
				return a;
			}
			if ((a = Avail("std_slegunhurt_lt_a_n")))
				return a;
		}
		if (LimbActive(2)) {
			if ((a = Avail("std_sarm_rt_n_n"))) {
				GeneralVoice("Scream", 5, 0.8f);
				return a;
			}
			if ((a = Avail("std_sarmunhurt_rt_a_n")))
				return a;
		}
		if (LimbActive(3)) {
			if ((a = Avail("std_sarm_lt_n_n"))) {
				GeneralVoice("Scream", 8, 0.8f);
				return a;
			}
			if ((a = Avail("std_sarmunhurt_lt_a_n")))
				return a;
		}
		vec3_t f, l, u, d;
		G3_EntityAxes(ent, f, l, u);
		VectorSubtract(point, ent->s.origin, d);
		if (DotProduct(f, d) >= 0) {
			if (damage > 75 && G3_irand(0, 5) == 0 && Avail("std_sblown_2bk_n_n") && Avail("lb_xgetup_n_a_n") &&
			    !(gore && gore->armourType == 3)) {
				GeneralVoice("Scream", 8, 0.8f);
				return Avail("std_sblown_2bk_n_n");
			}
			return First({"std_sunhurt_bk_mrs_2", "std_sunhurt_bk_pk_n"});
		}
		return First({"std_sunhurt_fwd_mrs_2", "std_sunhurt_fwd_pk_n"});
	}

	// ---------------------------------------------------------------------------------------------------
	// deaths (SPEC-722 "Death animation choice", "Dying follow-up", "Dying and corpse state")
	// ---------------------------------------------------------------------------------------------------
	const G3Anim *Death(const char *n, const char *voice, int variants, bool commit, bool mute)
	{
		const G3Anim *a = Avail(n);
		if (!a)
			return NULL;
		if (voice)
			GeneralVoice(voice, variants, 0.8f);
		if (commit)
			committed = true;
		if (mute)
			muted = true;
		return a;
	}

	bool AttackerBehind(edict_t *attacker)
	{
		if (!attacker)
			return false;
		vec3_t f, l, u, d;
		G3_EntityAxes(ent, f, l, u);
		VectorSubtract(attacker->s.origin, ent->s.origin, d);
		return DotProduct(f, d) < 0;
	}

	const G3Anim *ChooseDeath(edict_t *attacker, edict_t *inflictor, int damage, int dflags,
	                          const G3Anim *forced) override
	{
		(void)inflictor;
		(void)damage;
		SetSpeed4();
		CorpseState();
		const G3Anim *a;
		if (g3locks.deaths) {
			if (cur && cur->cls == ANIMCLS_DEATH)
				return cur;
			if (Posture() == POSTURE_SIT && (a = Avail("lb_sautofire_end_n_n")))
				return a;
			return Death("std_shead_2frnt_n_n", "Scream", 8, true, true);
		}
		if (ent->spawnflags & 0x10000)
			return Avail("lf_sautofire_end_n_n");
		if (forced && HasAnim(forced))
			return forced;
		if (tumbleFlag && (a = Avail("std_xtumble_2frnt_n_n"))) {
			if (!tumbling)
				GeneralVoice("Scream", 8, 0.8f);
			tumbling = true;
			committed = muted = true;
			return a;
		}
		if (cur && cur->cls == ANIMCLS_DEATH)
			return DyingFollowUp(dflags);
		int p = Posture();
		if (p == POSTURE_CROUCH) {
			if ((a = Death("cch_sdead_2bk_n_n", "Scream", 8, false, false)))
				return a;
			if (cur == Anim_Human("cch_xonknees_n_n_n") && (a = Death("cch_sonknees_2frnt_n_n", "Scream", 8, false, false)))
				return a;
		}
		if (p == POSTURE_PRONE && (a = Death("lf_sautofire_end_n_n", "Scream", 8, false, true)))
			return a;
		if (p == POSTURE_ONBACK)
			return DyingFollowUp(dflags);
		if (p == POSTURE_STAND) {
			if (dflags & DMGF_EXPLOSIVE) {
				if (AttackerBehind(attacker) && (a = Death("std_shead_2frnt_n_n", NULL, 0, true, true)))
					return a;
				if ((a = Death("std_sviolent_n_n_n", NULL, 0, true, true)))
					return a;
				if ((a = Death("std_stoknees_2bk_n_n", NULL, 0, true, true)))
					return a;
			}
			if (dflags & DMGF_MICROWAVE) {
				IGhoulInst *inst = Inst();
				if ((dflags & DMGF_MICROWAVE_RAY) && (a = Avail("std_snervegun_2bck_n_n"))) {
					GeneralVoice("Choke", 2, 0.8f);
					G3FX_OnEnt("environ/shockdeath", ent);
				} else if ((a = Avail("std_smicro_n_n_n"))) {
					GeneralVoice("Choke", 2, 0.8f);
					G3FX_OnEnt("environ/pulsecook", ent);
					G3_Sound(ent, CHAN_BODY, "Weapons/MPG/Expand.wav", 0.6f, ATTN_NORM);
				}
				if (a) {
					muted = true;
					int r = G3_irand(0, 4);
					if (inst)
						inst->SetSpeed(r == 0 ? 12 : 7 + r);
					return a;
				}
			}
			if (dflags & DMGF_FIRE) {
				static float lastBurnDeath = -999;
				float dt = G3_Time() - lastBurnDeath;
				if (!(dt >= -0.5f && dt < 0.1f) && (a = Death("std_sonfire_2bk_n_n", "Burn", 1, false, true))) {
					lastBurnDeath = G3_Time();
					return a;
				}
			}
			if ((dflags & DMGF_DANCE) && (a = Avail("std_sdance_n_n_n")))
				return a;
			if (gore && gore->killZone >= 0) { // the first hit zone of the first killing hit (G3.1, G3.13)
				a = ZoneDeath(gore->killZone);
				if (a)
					return a;
			}
		}
		if (G3_irand(0, 3) != 0) {
			if (!attacker || !AttackerBehind(attacker)) {
				if (G3_irand(0, 2) == 0 && (a = Death("std_schest_2frnt_n_n", "Scream", 8, false, true)))
					return a;
				if ((a = Death("std_sblown_2bk_n_n", "Scream", 8, true, true)))
					return a;
				if ((a = Death("std_sviolent_n_n_n", "Scream", 8, false, true)))
					return a;
			} else if ((a = Death("std_sinback_2frnt_n_n", "Scream", 8, true, true)))
				return a;
		}
		vec3_t v;
		if (owner)
			owner->Velocity(v);
		else
			VectorCopy(ent->velocity, v);
		if (G3_irand(0, 3) != 0 && DotProduct(v, v) > 40 &&
		    (a = Avail(G3_irand(0, 1) ? "std_sstumble_2frnt_n_n" : "std_sstumble_2bk_p_n")))
			return a;
		if ((a = Death("std_stoknees_2bk_n_n", "Scream", 8, true, true)))
			return a;
		return Avail("std_shead_2frnt_n_n");
	}

	const G3Anim *ZoneDeath(int z)
	{
		int seg = gore->zones[z].segment;
		const G3Anim *a;
		bool cine = G3_Cinematic();
		switch (seg) {
		case SEG_NECK:
			if ((a = Death("std_sthroat_2side_n_n", "Choke", 2, false, false))) {
				G3FX_StumpBlood(ent, G3G_Part(Inst(), "sbolt_mouth"));
				if (!cine && g3hooks.statistic)
					g3hooks.statistic(2);
				return a;
			}
			G3_Printf("eek. That hurt my neck.\n");
			return NULL;
		case SEG_HEAD:
			if ((a = Avail("std_shead_2frnt_n_n"))) {
				if (!cine && g3hooks.statistic)
					g3hooks.statistic(1);
				return a;
			}
			return NULL;
		case SEG_GROIN:
			if ((a = Death("std_sgroin_2side_n_n", "Crotch", 2, false, false))) {
				if (!cine && g3hooks.statistic)
					g3hooks.statistic(3);
				return a;
			}
			G3_Printf("eek. That hurt my groin.\n");
			return NULL;
		case SEG_LTHIGH: case SEG_LCALF: case SEG_LFOOT:
			if ((a = Death("std_sleglt_2bk_n_n", "Scream", 8, false, false)))
				return a;
			G3_Printf(seg == SEG_LFOOT ? "eek. That hurt my left foot.\n" : "Couldn't find left leg death!\n");
			return NULL;
		case SEG_RTHIGH: case SEG_RCALF: case SEG_RFOOT:
			if ((a = Death("std_slegrt_2bk_n_n", "Scream", 8, false, false)))
				return a;
			G3_Printf(seg == SEG_RFOOT ? "eek. That hurt my right foot.\n" : "Couldn't find right leg death!\n");
			return NULL;
		case SEG_SHOULDER:
			if (z == GZ_SHLDR_RIGHT_FRONT || z == GZ_SHLDR_LEFT_BACK) {
				if ((a = Death("std_sshoulder_rt_n_n", "Scream", 8, false, false)))
					return a;
				G3_Printf("Couldn't find right shoulder death!\n");
			} else {
				if ((a = Death("std_sshoulder_lt_n_n", "Scream", 8, false, false)))
					return a;
				G3_Printf("Couldn't find left shoulder death!\n");
			}
			return NULL;
		}
		if (z == GZ_GUT_FRONT) {
			if ((a = Death("std_sgut_2side_n_n", "Scream", 8, false, false)))
				return a;
			G3_Printf("Couldn't find gut death!\n");
		}
		return NULL;
	}

	bool HeadAttached(void)
	{
		return gore && gore->OwnerShown(GZ_HEAD_FRONT);
	}

	const G3Anim *DyingFollowUp(int dflags)
	{
		float since = G3_Time() - playStart;
		const G3Anim *a;
		if (cur == Anim_Human("std_sdance_n_n_n")) {
			if (since <= 0.3f) {
				if (G3_irand(0, 7) == 0)
					GeneralVoice(expr == EXPR_DEAD ? "Hit" : "Scream", expr == EXPR_DEAD ? 3 : 8, 0.8f);
				return cur;
			}
			if ((a = Death("std_shead_2frnt_n_n", NULL, 0, true, true)))
				return a;
		}
		if (cur == Anim_Human("lb_sautofire_loop_n_n") && since > 0.3f &&
		    (a = Death("lb_sautofire_end_n_n", NULL, 0, true, true)))
			return a;
		if (cur && ((cur->flags & ANIMF_REHITTABLE) || AnimDone()) &&
		    (cur->posture == POSTURE_ONBACK || cur->posture == POSTURE_ONSIDE) && since > 0.1f &&
		    G3_Time() - lastHitTime <= 0.1f) {
			if (cur->posture == POSTURE_ONSIDE)
				ent->s.angles[YAW] = anglemod(ent->s.angles[YAW] + 180);
			if ((dflags & DMGF_DANCE) && (a = Avail("lb_sautofire_loop_n_n")))
				return a;
			if ((a = Avail("lb_sautofire_end_n_n"))) {
				pendingRestart = true;
				return a;
			}
		}
		if (cur == Anim_Human("lf_sautofire_loop_n_n") && since > 0.3f && (a = Avail("lf_sautofire_end_n_n")))
			return a;
		if (cur && cur->posture == POSTURE_FACEDOWN && ((cur->flags & ANIMF_REHITTABLE) || AnimDone()) &&
		    G3_Time() - lastHitTime <= 0.1f) {
			if ((dflags & DMGF_DANCE) && (a = Avail("lf_sautofire_loop_n_n")))
				return a;
			if ((a = Avail("lf_sautofire_end_n_n")))
				return a;
		}
		struct { const char *loop, *die; } writhe[3] = {{"lb_pgeneric_n_n_n", "lb_pgenericdie_n_n_n"},
			{"lb_pholdleg_rt_n_n", "lb_pholdlegdie_rt_n_n"}, {"lb_pholdleg_lt_n_n", "lb_pholdlegdie_lt_n_n"}};
		for (auto &w : writhe)
			if (cur == Anim_Human(w.loop)) {
				if (AnimDone() && since > writheTime)
					if ((a = Death(w.die, NULL, 0, true, true)))
						return a;
				return cur;
			}
		if (AnimDone() && !committed && cur) {
			if (cur == Anim_Human("std_slegrt_2bk_n_n") && G3_irand(0, 2) == 0 && HeadAttached() &&
			    (a = Death("lb_pholdlegto_rt_n_n", "Writhe", 7, false, true)))
				return a;
			if (cur == Anim_Human("lb_pholdlegto_rt_n_n") && (a = Avail("lb_pholdleg_rt_n_n"))) {
				writheTime = 3 + G3_flrand(0, 3);
				return a;
			}
			if (cur == Anim_Human("std_sleglt_2bk_n_n") && G3_irand(0, 2) == 0 && HeadAttached() &&
			    (a = Death("lb_pholdlegto_lt_n_n", "Writhe", 7, false, true)))
				return a;
			if (cur == Anim_Human("lb_pholdlegto_lt_n_n") && (a = Avail("lb_pholdleg_lt_n_n"))) {
				writheTime = 3 + G3_flrand(0, 3);
				return a;
			}
			if (cur == Anim_Human("lb_pgenericto_n_n_n") && (a = Avail("lb_pgeneric_n_n_n"))) {
				writheTime = 3 + G3_flrand(0, 3);
				return a;
			}
			if (!(cur->flags & ANIMF_LOOP) && G3_irand(0, 2) == 0 && HeadAttached() &&
			    (a = Death("lb_pgenericto_n_n_n", "Writhe", 7, false, true)))
				return a;
		}
		committed = muted = true;
		return cur;
	}
	float writheTime = 3;

	void CorpseState(void)
	{
		if (ent->health > 0)
			return;
		float now = G3_Time();
		if (deathTime == 0)
			deathTime = firstDeathTime = now;
		if (cur && cur->cls == ANIMCLS_DEATH && AnimDone() && !deathNotified) {
			float pctf = ArmourPercentF();
			int pct = (int)pctf;
			if (pctf > 0 && !armourDropped && !G3_ClassIs(ent, "m_x_mraiderboss1") &&
			    !G3_ClassIs(ent, "m_x_mraiderboss2")) {
				if (g3hooks.dropArmour)
					g3hooks.dropArmour(ent, pct > 50 ? pct : 50);
				StripArmour();
				armourDropped = true;
			}
			ReleaseStuckKnives(NULL);
			if (owner && owner->HasKiller())
				owner->OnDeathAnimDone();
			deathNotified = true;
		}
		if (AnimDone() && expr != EXPR_DEAD && (committed || now - playStart > 10.0f))
			SetExpression(EXPR_DEAD, 999999.9f, false);
		if (frozenAt == 0 && (AnimDone() || now - deathTime > 5.0f)) {
			IGhoulInst *inst = Inst();
			if (inst) {
				inst->Pause(now);
				int n = inst->NumChildren();
				for (int i = 0; i < n; i++) {
					GhoulID b;
					IGhoulInst *c = inst->GetChild(i, &b);
					if (c)
						c->Pause(now);
				}
			}
			frozenAt = now;
		}
		if (cur == Anim_Human("std_smicro_n_n_n") && AnimDone() && !g3locks.deaths && gore && !gibbed) {
			G3Hit h;
			memset(&h, 0, sizeof h);
			h.targ = ent;
			h.dflags = DMGF_MICROWAVE;
			gore->Gib(h);
			G3_Sound(ent, CHAN_AUTO, "Weapons/MPG/Burst.wav", 1.0f, ATTN_NORM);
		}
	}

	// ---------------------------------------------------------------------------------------------------
	// weapon handling
	// ---------------------------------------------------------------------------------------------------
	int charRef(void) { return G_EdictNum(ent); }

	bool ReadyToFire(void) override
	{
		if (!Armed())
			return false;
		if (rightType == 16 && lastShot < -900) {
			lastShot = G3_Time() - 1.0f;
			return false;
		}
		float d = Body_RefireDelay(rightType, charRef());
		float F = owner ? owner->RefireFactor() : 1.0f;
		float h = LimbInjury() ? 2.0f : 1.0f;
		float p = (owner && owner->IsDesignated()) ? 0.75f : 1.0f;
		return G3_Time() - lastShot > d * F * h * p;
	}

	bool Fire(edict_t *target, const vec3_t targetPoint, bool paced, int targetBolt) override
	{
		(void)targetBolt;
		if (!Armed())
			return false;
		float now = G3_Time();
		if (paced) {
			float d = Body_RefireDelay(rightType, charRef());
			float F = owner ? owner->RefireFactor() : 1.0f;
			float q = (owner && owner->IsDesignated()) ? 0.5f : 1.0f;
			float i = LimbInjury() ? 2.5f : 1.0f;
			if (now - lastShot < d * F * q * i)
				return false;
		}
		IGhoulInst *inst = Inst();
		IGhoulInst *w = G3G_ChildOnBolt(inst, boltHandR);
		GhoulID flash = G3G_Part(w, "flash");
		if (!w || !flash)
			return false;
		vec3_t start, dir;
		if (!G3G_BoltWorld(ent, w, flash, start))
			VectorCopy(ent->s.origin, start);
		VectorSubtract(targetPoint, start, dir);
		VectorNormalize(dir);
		if (!(owner && owner->FiresBlind())) {
			vec3_t f, l, u;
			G3_EntityAxes(ent, f, l, u);
			/* the facing test is horizontal: a crouched character with a player standing close in front of it (tsr1
			 * `tokenguy2` ~25 units away, kos1 shotgunner) aims steeply up or down at the near-range hit point, the 3D
			 * dot dropped under 0.717 on most shots and the shot was refused; the original fires in both cases */
			float fl = sqrtf(f[0] * f[0] + f[1] * f[1]), hl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);
			float dot = (fl > 1e-4f && hl > 1e-4f) ? (f[0] * dir[0] + f[1] * dir[1]) / (fl * hl) : 1.0f;
			if (dot < 0 || (dot < 0.717f && rightType != 18))
				return false; // facing away: no shot, the round is kept
		}
		float s = AimSpread(rightType);
		if (s > 0) {
			for (int k = 0; k < 3; k++)
				dir[k] += G3_flrand(-s, s);
			VectorNormalize(dir);
		}
		// SPEC-722 Firing: for types 7, 9, 11, 12, 16, 17, 18 and 29 the vertical aim uses the target's real position
		// (its origin, SPEC-AMEND-006 G2.11), and type 11 then mixes 60 % of the current direction with 40 % towards
		// the target; applied in the spec's order, after the per-type spread. Without it the full +-0.2 spread of a
		// shotgunner (type 11) sent pellets into characters next to the line of fire.
		if (target && target->inuse && !(owner && owner->FiresBlind())) {
			int at = rightType;
			if (at == 7 || at == 9 || at == 11 || at == 12 || at == 16 || at == 17 || at == 18 || at == 29) {
				vec3_t to;
				VectorSubtract(target->s.origin, start, to);
				float th = sqrtf(to[0] * to[0] + to[1] * to[1]), dh = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);
				if (th > 1.0f && dh > 1e-4f) {
					dir[2] = dh * to[2] / th;
					VectorNormalize(dir);
				}
				if (at == 11) {
					VectorNormalize(to);
					for (int k = 0; k < 3; k++)
						dir[k] = 0.6f * dir[k] + 0.4f * to[k];
					VectorNormalize(dir);
				}
			}
		}
		clip--;
		lastShot = now;
		justShot = true;
		if (g3hooks.fireAttack)
			g3hooks.fireAttack(ent, rightType, start, dir, target);
		if (target && target->client && g3hooks.musicDanger)
			g3hooks.musicDanger(target);
		return true;
	}

	bool CanThrowNow(void) override
	{
		if (!leftType || leftType == 27)
			return false;
		if (!(ent->spawnflags & 0x80000) && leftType != 30)
			return false;
		return G3_flrand(0, 1) < 0.1f;
	}

	bool Throw(edict_t *target) override
	{
		if (!leftType || leftType == 27 || !target)
			return false;
		vec3_t d;
		VectorSubtract(target->s.origin, ent->s.origin, d);
		if (leftType != 30 && VectorLength(d) < 200)
			return false;
		vec3_t start;
		if (!G3G_BoltWorldByName(ent, "wbolt_hand_l", start))
			VectorCopy(ent->s.origin, start);
		G3_Sound(ent, CHAN_AUTO, "Weapons/gren/throw.wav", 0.6f, ATTN_NORM);
		if (g3hooks.fireAttack) {
			vec3_t dir;
			VectorSubtract(target->s.origin, start, dir);
			VectorNormalize(dir);
			g3hooks.fireAttack(ent, leftType, start, dir, target);
		}
		return true;
	}

	// savegame: the human part of the body image (OpenMerc layout, g3_save.cpp); Ghoul ids and instance pointers are
	// looked up again after loading (hand bolts lazily, the grenade bolt-on by model)
	void SaveFields(G3SaveBuf &b) override
	{
		b.I(rightType, 2);
		b.I(leftType, 2);
		b.Str(facePrefix, sizeof facePrefix);
		b.Str(faceSuffix, sizeof faceSuffix);
		b.F(blinkAt);
		b.F(breathAt);
		b.Str(voiceDir, sizeof voiceDir);
		b.I(voiceNum, 1);
		b.I(deathType, 1);
		b.Str(customDir, sizeof customDir);
		b.I(numWake, 1);
		b.I(numGreet, 1);
		b.I(greetIndex, 1);
		b.F(greetTime);
		b.I(clip, 2);
		b.F(lastShot);
		b.B(justShot);
		for (int i = 0; i < 4; i++)
			b.F(limbExpire[i]);
		b.B(armHitPending);
		b.B(committed);
		b.B(tumbling);
		b.B(tumbleFlag);
		b.B(deathNotified);
		b.B(armourDropped);
		b.F(lastPainTime);
		b.B(firstHitTrigger);
		b.I(scriptedKill, 1);
		b.Anim(prevAnim);
		b.F(startledDone);
		b.I(lastFidget, 2);
		b.I(chargeTick, 2);
		if (b.reading) {
			boltHandR = boltHandL = 0;
			hasPending = false;
			IGhoulInst *inst = Inst();
			grenadeInst = inst ? G3G_ChildOnBolt(inst, G3G_Part(inst, "wbolt_hand_l")) : NULL;
			if (grenadeInst && Q_stricmp(G3G_ModelName(grenadeInst->GetGhoulObject()), "grenade"))
				grenadeInst = NULL;
		}
	}
};

G3Body *G3_NewHumanBody(edict_t *ent, int family)
{
	return new G3HumanBody(ent, family);
}

// scripted kill (SPEC-720 11.1, dogs B12): variant 2 adds SPREAD. Also G7's fx::g_arm_scripted_kill hook
// (environ_invisible_attack mouth shot), installed by Gore_LevelStart.
void G3_DogScriptedKill(G3Body *b, int variant);
void Body_ScriptedKill(edict_t *ent, int variant)
{
	G3Body *b = Body_Of(ent);
	if (b && G3_IsHumanFamily(b->family))
		static_cast<G3HumanBody *>(b)->scriptedKill = variant;
	else
		G3_DogScriptedKill(b, variant);
}

void Body_SetFirstHitTrigger(edict_t *ent)
{
	G3Body *b = Body_Of(ent);
	if (b && b->family == BODY_MESO_ARMOURED)
		static_cast<G3HumanBody *>(b)->firstHitTrigger = true;
}

void Body_SetTumble(edict_t *ent, bool on)
{
	G3Body *b = Body_Of(ent);
	if (b && G3_IsHumanFamily(b->family))
		static_cast<G3HumanBody *>(b)->tumbleFlag = on;
}
