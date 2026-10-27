// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - the animal bodies (husky, rottweiler, cow/bull) and the vehicle bodies
// (helicopter, tank, snowcat): their animation choices, sounds, gore and hit handling.
// Implements: SPEC-722 common part (Cow / bull body, Dog bodies, Helicopter body, Tank body, Snowcat body),
//             SPEC-720 Behaviour 12 (dog, cow, helicopter, tank and snowcat hits), SPEC-721 §12, SPEC-731 Part B §2
#include "g3_local.h"

using namespace ghoul;

// zones of a traced hit for animal/vehicle bodies: distinct zones of the body records, in order
static int RecordZones(G3Body *b, int *out, int max, int *firstZone, int *exitZone)
{
	*firstZone = *exitZone = -1;
	IGhoulInst *inst = b->Inst();
	if (!b->gore || dmg_shot.ent != b->ent)
		return 0;
	int n = 0;
	for (int i = 0; i < dmg_shot.count; i++) {
		const HitRecord &r = dmg_shot.rec[i];
		if (r.inst != inst || !r.part)
			continue;
		vec3_t p = {r.point[0], r.point[1], r.point[2]};
		int z = b->gore->ZoneByOwnerPart(r.part, p, false);
		if (z < 0)
			continue;
		if (*firstZone < 0)
			*firstZone = z;
		*exitZone = z;
		bool dup = false;
		for (int k = 0; k < n; k++)
			dup |= out[k] == z;
		if (!dup && n < max)
			out[n++] = z;
	}
	return n;
}

static bool NoGhoulHits(G3Body *b)
{
	return dmg_shot.ent != b->ent || dmg_shot.count == 0;
}

// gore of an animal hit (SPEC-721 §12)
static void AnimalGore(G3Body *b, int entry, int exitz, int d, int H, const G3Hit &h)
{
	G3Gore *g = b->gore;
	if (!g || entry < 0)
		return;
	g->NoteKillingHit(entry, d, H);
	g->WoundZone(entry, d, H, h.mod, h.dflags, h.attacker, h.dir);
	if (exitz >= 0 && exitz != entry && !g3locks.gorezones) {
		G3Zone &z = g->zones[exitz];
		if (z.woundMesh && g->OwnerShown(exitz)) {
			z.damage = 201;
			z.level = 3;
			b->Inst()->SetPartOnOff(z.woundMesh, true);
			g->ApplyWoundSkin(exitz, 3, G3_irand(0, 0x7fff) % 3);
		}
	}
	if (g->CanSever(entry, d, H, h.dflags)) {
		G3Zone &z = g->zones[entry];
		edict_t *p = g->Sever(entry, -1, false, vec3_origin, false);
		(void)p;
		if (z.bodyCapBolt) {
			if (H - d <= 0)
				G3FX_BloodCloud(b->ent, z.bodyCapBolt, 100);
			else
				G3FX_StumpBlood(b->ent, z.bodyCapBolt);
		}
	}
	g->KillingCloud(entry, d, H);
}

// ---------------------------------------------------------------------------------------------------------------
// dogs
// ---------------------------------------------------------------------------------------------------------------
class G3DogBody : public G3Body {
public:
	float lastBark, lastBite, lastPain, attackTime;
	int scriptedKill;
	G3DogBody(edict_t *e, int fam) : G3Body(e, fam)
	{
		modelDir = fam == BODY_HUSKY ? "enemy/husky" : "enemy/rottweiler";
		lastBark = lastBite = lastPain = attackTime = -999;
		scriptedKill = 0;
		gore = G3_NewGore(this, 10);
		if (gore) {
			for (int i = 0; i < 10; i++)
				gore->zones[i].scale = i == 0 ? 10.0f : 1.0f;
		}
		ent->deadflag = 0;
	}
	void SaveFields(G3SaveBuf &b) override
	{
		b.F(lastBark);
		b.F(lastBite);
		b.F(lastPain);
		b.F(attackTime);
		b.I(scriptedKill, 1);
	}
	const G3Anim *Rec(int i) { return &g3_dogAnims[i]; }
	const G3Anim *ByName(const char *n)
	{
		const G3Anim *a = Anim_Find(g3_dogAnims, 26, n);
		return (a && HasAnim(a)) ? a : NULL;
	}
	void Sound(const char *n, float *last)
	{
		if (G3_Time() - *last < 0.5f)
			return;
		*last = G3_Time();
		G3_Sound(ent, CHAN_BODY, n, 0.8f, ATTN_NORM);
	}
	void Bark(void)
	{
		char s[64];
		Com_sprintf(s, sizeof s, "Enemy/Dog/Bark%d.wav", G3_irand(1, 3));
		Sound(s, &lastBark);
	}
	void Precache(void) override
	{
		static const char *const s[] = {"Enemy/Dog/Bark1.wav", "Enemy/Dog/Bark2.wav", "Enemy/Dog/Bark3.wav",
			"Enemy/Dog/Bite1.wav", "Enemy/Dog/Bite2.wav", "Enemy/Dog/Die.wav", NULL};
		for (int i = 0; s[i]; i++)
			G3_SoundIndex(s[i]);
		if (gore)
			gore->Setup();
		RegisterEvents();
	}
	int RightHandType(void) override { return 20; }
	const G3Anim *ChooseIdle(void) override
	{
		const G3Anim *a;
		if (cur && cur->cls == ANIMCLS_STATIONARY && !AnimDone())
			return cur;
		if (cur == ByName("std_itosit_n") && (a = ByName("sit_i_n")))
			return a;
		if (cur == ByName("std_wsniff_n") && (a = ByName("std_wsniff_frm")))
			return a;
		bool sitting = cur == ByName("sit_i_n");
		if (sitting) {
			if (G3_irand(0, 4) == 0 && (a = ByName("sit_itostand_n")))
				return a;
			return cur;
		}
		if (G3_irand(0, 4) == 0 && (a = ByName("std_itosit_n")))
			return a;
		if (owner && owner->Target() && G3_irand(0, 2) == 0 && cur != ByName("std_ibark_n") &&
		    (a = ByName("std_ibark_n"))) {
			Bark();
			return a;
		}
		if ((a = ByName("std_ipant_n")))
			return a;
		return ByName("std_w_n");
	}
	const G3Anim *ChooseStand(const G3MoveInput &in) override
	{
		(void)in;
		return ChooseIdle();
	}
	const G3Anim *ChooseMove(const G3MoveInput &in) override
	{
		if (cur && cur->cls == ANIMCLS_MOVE && !AnimDone())
			return cur;
		const G3Anim *a;
		if (cur == ByName("sit_i_n") && (a = ByName("sit_itostand_n")))
			return a;
		if (in.target)
			return ChooseAttack(in);
		const G3Anim *w = ByName("std_w_n");
		if (!cur || cur->cls == ANIMCLS_STATIONARY)
			return w ? w : cur;
		const char *n = cur->name;
		if (!Q_stricmp(n, "std_w_n") || !Q_stricmp(n, "std_wlookltrt_n")) {
			if (!Q_stricmp(n, "std_w_n") && G3_irand(0, 2) != 0)
				return cur;
			int r = G3_irand(0, 2);
			a = ByName(r == 0 ? "std_wlooklt_to" : (r == 1 ? "std_wlookrt_to" : "std_wlookltrt_n"));
			return a ? a : cur;
		}
		if (!Q_stricmp(n, "std_wlooklt_to"))
			return (a = ByName("std_wlooklt_n")) ? a : cur;
		if (!Q_stricmp(n, "std_wlookrt_to"))
			return (a = ByName("std_wlookrt_n")) ? a : cur;
		if (!Q_stricmp(n, "std_wlooklt_n") || !Q_stricmp(n, "std_wlookrt_n")) {
			if (G3_irand(0, 2) != 0)
				return cur;
			a = ByName(!Q_stricmp(n, "std_wlooklt_n") ? "std_wlooklt_frm" : "std_wlookrt_frm");
			return a ? a : cur;
		}
		if (!Q_stricmp(n, "std_wlooklt_frm") || !Q_stricmp(n, "std_wlookrt_frm"))
			return w ? w : cur;
		if (!Q_stricmp(n, "std_wsniff_n"))
			return (a = ByName("std_wsniff_frm")) ? a : cur;
		return cur;
	}
	const G3Anim *ChooseJump(const G3MoveInput &in) override
	{
		(void)in;
		return ByName("std_w_n");
	}
	const G3Anim *ChooseAttack(const G3MoveInput &in) override
	{
		attackTime = G3_Time();
		const G3Anim *lunge = Rec(6), *bite = Rec(8), *a;
		if (cur == lunge && !AnimDone())
			return cur;
		if (cur == ByName("sit_i_n") && (a = ByName("sit_itostand_n")))
			return a;
		if (cur == ByName("std_wsniff_n") && (a = ByName("std_wsniff_frm")))
			return a;
		edict_t *t = in.target;
		if (!t)
			return ByName("std_w_n");
		vec3_t d, f, r, u;
		VectorSubtract(t->s.origin, ent->s.origin, d);
		float D = VectorNormalize(d);
		AngleVectors(ent->s.angles, f, r, u);
		const float R = 85;
		if (DotProduct(f, d) > 0.5f) {
			if (D >= 0.8f * R && D <= 1.2f * R)
				return HasAnim(lunge) ? lunge : NULL;
			if (D < 0.8f * R) {
				if (cur != bite)
					Bark();
				return HasAnim(bite) ? bite : NULL;
			}
			if (G3_irand(0, 3) == 0)
				Bark();
			return ByName("std_r_n");
		}
		if (G3_irand(0, 19) == 0)
			Bark();
		return ByName("std_w_n");
	}
	const G3Anim *ChooseDeath(edict_t *attacker, edict_t *inflictor, int damage, int dflags,
	                          const G3Anim *forced) override
	{
		(void)attacker;
		if (forced && HasAnim(forced))
			return forced;
		if (cur && cur->cls == ANIMCLS_DEATH && !(cur->flags & ANIMF_REHITTABLE) && !AnimDone())
			return cur;
		if (ent->health <= 0 && cur && cur->cls == ANIMCLS_DEATH) {
			const G3Anim *p1 = Rec(22), *p2 = Rec(23), *p3 = Rec(24);
			if (cur == p3 && G3_Time() - lastHitTime > 0.3f)
				return HasAnim(p1) ? p1 : ByName("std_w_n");
			if (((cur->flags & ANIMF_REHITTABLE) || AnimDone()) && G3_Time() - lastHitTime <= 0.1f && cur != p2) {
				pendingRestart = true;
				const G3Anim *t = (lastHitFlags & DMGF_DANCE) ? p3 : p1;
				if (HasAnim(t))
					return t;
				return ByName("std_w_n");
			}
			return cur;
		}
		const G3Anim *a;
		if (damage <= 90) {
			bool running = cur == Rec(2);
			a = running ? Rec(9) : Rec(3);
			if (!HasAnim(a))
				a = ByName("std_w_n");
			return a;
		}
		a = Rec(25);
		if (inflictor) {
			vec3_t d;
			VectorSubtract(inflictor->s.origin, ent->s.origin, d);
			ent->s.angles[YAW] = vectoyaw(d);
		}
		(void)dflags;
		return HasAnim(a) ? a : ByName("std_w_n");
	}
	const G3Anim *ChoosePain(edict_t *, const vec3_t, int, int) override
	{
		if (ent->health > 0)
			Sound("Enemy/Dog/Die.wav", &lastPain);
		return NULL;
	}
	void OnNote(int note, const char *arg) override
	{
		G3Body::OnNote(note, arg);
		if (note == 2 /* fire */) {
			// bite: attack type 20 from the tongue bolt along the AI's aim
			vec3_t start, dir;
			if (!G3G_BoltWorldByName(ent, "SBOLT_HUSKY_TONGUE", start))
				VectorCopy(ent->s.origin, start);
			vec3_t f, r, u;
			AngleVectors(ent->s.angles, f, r, u);
			VectorCopy(f, dir);
			edict_t *t = owner ? owner->Target() : NULL;
			if (g3hooks.fireAttack)
				g3hooks.fireAttack(ent, 20, start, dir, t);
			if (owner && t)
				owner->OnDogBite(t);
			char s[64];
			Com_sprintf(s, sizeof s, "Enemy/Dog/Bite%d.wav", G3_irand(1, 2));
			Sound(s, &lastBite);
		}
	}
	int Damage(G3Hit &h) override
	{
		Dmg_EnsureShot(ent, h);
		if (scriptedKill) {
			h.damage = 9999;
			if (scriptedKill == 2)
				h.dflags |= DMGF_SPREAD;
			scriptedKill = 0;
		}
		if ((h.dflags & DMGF_PROJECTILE) && NoGhoulHits(this))
			return 0;
		int zones[8], first, exitz;
		int n = RecordZones(this, zones, 8, &first, &exitz);
		int d = h.damage;
		for (int i = 0; i < n; i++)
			d = (int)(d * gore->zones[zones[i]].scale);
		lastHitTime = G3_Time();
		lastHitFlags = h.dflags;
		AnimalGore(this, first, exitz, d, ent->health, h);
		return d;
	}
};

// ---------------------------------------------------------------------------------------------------------------
// cow / bull
// ---------------------------------------------------------------------------------------------------------------
class G3CowBody : public G3Body {
public:
	float lastMoo, lastPain, attackTime;
	void SaveFields(G3SaveBuf &b) override
	{
		b.F(lastMoo);
		b.F(lastPain);
		b.F(attackTime);
	}
	G3CowBody(edict_t *e) : G3Body(e, BODY_COW)
	{
		modelDir = "enemy/bull";
		lastMoo = lastPain = attackTime = -999;
		gore = G3_NewGore(this, 6);
		ent->deadflag = 0;
	}
	const G3Anim *ByName(const char *n)
	{
		const G3Anim *a = Anim_Find(g3_cowAnims, 6, n);
		return (a && HasAnim(a)) ? a : NULL;
	}
	void Precache(void) override
	{
		G3_SoundIndex("Enemy/Cow/Moo1.wav");
		G3_SoundIndex("Enemy/Cow/Moo2.wav");
		G3_SoundIndex("Enemy/Cow/Die.wav");
		if (gore)
			gore->Setup();
		RegisterEvents();
	}
	int RightHandType(void) override { return 20; }
	const G3Anim *ChooseIdle(void) override
	{
		if (cur && cur->cls == ANIMCLS_STATIONARY && !AnimDone())
			return cur;
		const G3Anim *a;
		if (G3_irand(0, 59) == 0 && (a = ByName("std_imoo_n"))) {
			if (G3_Time() - lastMoo >= 30.0f) {
				lastMoo = G3_Time();
				G3_Sound(ent, CHAN_BODY, G3_irand(0, 1) ? "Enemy/Cow/Moo1.wav" : "Enemy/Cow/Moo2.wav", 0.8f, ATTN_NORM);
			}
			return a;
		}
		if (G3_irand(0, 59) == 0 && (a = ByName("std_ilooking_n")))
			return a;
		return ByName("std_i_n");
	}
	const G3Anim *ChooseStand(const G3MoveInput &) override { return ChooseIdle(); }
	const G3Anim *ChoosePain(edict_t *, const vec3_t, int, int) override
	{
		if (ent->health > 0 && G3_Time() - lastPain >= 0.5f) {
			lastPain = G3_Time();
			G3_Sound(ent, CHAN_BODY, "Enemy/Cow/Die.wav", 0.9f, ATTN_NORM);
		}
		const G3Anim *a = ByName("std_p_n");
		return a ? a : cur;
	}
	const G3Anim *ChooseDeath(edict_t *, edict_t *, int, int, const G3Anim *forced) override
	{
		if (forced && HasAnim(forced))
			return forced;
		if (cur && cur->cls == ANIMCLS_DEATH && !(cur->flags & ANIMF_REHITTABLE) && !AnimDone())
			return cur;
		return ByName("std_s_rt");
	}
	const G3Anim *ChooseAttack(const G3MoveInput &) override
	{
		attackTime = G3_Time();
		return NULL;
	}
	int Damage(G3Hit &h) override
	{
		Dmg_EnsureShot(ent, h);
		if ((h.dflags & DMGF_PROJECTILE) && NoGhoulHits(this))
			return 0;
		int zones[6], first, exitz;
		RecordZones(this, zones, 6, &first, &exitz);
		AnimalGore(this, first, exitz, h.damage, ent->health, h);
		return h.damage;
	}
};

G3Body *G3_NewDogBody(edict_t *ent, int family)
{
	return new G3DogBody(ent, family);
}

// scripted kill on a dog body (SPEC-720 11.1 / B12; Body_ScriptedKill)
void G3_DogScriptedKill(G3Body *b, int variant)
{
	if (b && (b->family == BODY_HUSKY || b->family == BODY_ROTTWEILER))
		static_cast<G3DogBody *>(b)->scriptedKill = variant;
}

G3Body *G3_NewCowBody(edict_t *ent)
{
	return new G3CowBody(ent);
}

// ---------------------------------------------------------------------------------------------------------------
// helicopter
// ---------------------------------------------------------------------------------------------------------------
static void RicochetSparks(G3Body *b, const vec3_t at, const G3Hit &h, float *last)
{
	if (G3_Time() - *last < 0.3f)
		return;
	*last = G3_Time();
	vec3_t d;
	VectorNegate(h.dir, d);
	G3FX_Sparks(0, at, d);
	char s[64];
	Com_sprintf(s, sizeof s, "Weapons/FX/Rics/ric%d.wav", G3_irand(1, 6));
	G3_Sound(b->ent, CHAN_AUTO, s, 1.0f, ATTN_NORM);
}

// helicopter hit passed on (SPEC-AMEND-008 G4): spark type 0, 10 or 14 at random, placed at the hit zone's stored
// reference point and direction moved with the helicopter. The stock helicopters store zero for both, so the sparks
// sit at the entity origin with a zero direction. One ric sound on the helicopter's channel 5.
static void HeliRicochet(G3Body *b, float *last)
{
	if (G3_Time() - *last < 0.3f)
		return;
	*last = G3_Time();
	static const int types[3] = {0, 10, 14};
	vec3_t zero = {0, 0, 0};
	G3FX_Sparks(types[G3_irand(0, 2)], b->ent->s.origin, zero);
	char s[64];
	Com_sprintf(s, sizeof s, "Weapons/FX/Rics/ric%d.wav", G3_irand(1, 6));
	G3_Sound(b->ent, 5, s, 1.0f, ATTN_NORM);
}

static bool AttackerWeapon(const G3Hit &h, int weapon)
{
	return h.attacker && h.attacker->client && h.attacker->client->inv &&
	       h.attacker->client->inv->CurrentWeapon() == weapon;
}

class G3HeliBody : public G3Body {
public:
	float lastRic;
	int crewShots;          // sniper counter (shared by pilot and gunner: UNVERIFIED)
	bool crewKillAllowed;
	bool gunnerDead, pilotDead;
	void SaveFields(G3SaveBuf &b) override
	{
		b.F(lastRic);
		b.I(crewShots, 2);
		b.B(crewKillAllowed);
		b.B(gunnerDead);
		b.B(pilotDead);
	}
	G3HeliBody(edict_t *e) : G3Body(e, BODY_HELI)
	{
		lastRic = -999;
		crewShots = 1;
		crewKillAllowed = true;
		gunnerDead = pilotDead = false;
		IGhoulInst *inst = Inst();
		if (inst)
			inst->StartNoteEvents(G3_Time());
	}
	IGhoulInst *Head(const char *bolt)
	{
		return G3G_ChildOnBolt(Inst(), G3G_Part(Inst(), bolt));
	}
	int Damage(G3Hit &h) override
	{
		Dmg_EnsureShot(ent, h);
		if (NoGhoulHits(this))
			return 0;
		const HitRecord &r = dmg_shot.rec[0];
		IGhoulInst *gunner = Head("body1"), *pilot = Head("body2"), *canopy = Head("canopy");
		vec3_t at;
		G3_EntityToWorld(ent, r.point, at);
		if (r.inst && (r.inst == gunner || r.inst == pilot)) {
			if (AttackerWeapon(h, 6) && crewKillAllowed) {
				if (--crewShots <= 0) {
					if (r.inst == gunner)
						gunnerDead = true;
					else
						pilotDead = true;
				}
				return h.damage;
			}
			HeliRicochet(this, &lastRic);
			return h.damage;
		}
		if (r.inst && r.inst == canopy) {
			HeliRicochet(this, &lastRic);
			return h.damage;
		}
		HeliRicochet(this, &lastRic);
		return (int)(h.damage * 0.1f);
	}
};

G3Body *G3_NewHeliBody(edict_t *ent)
{
	return new G3HeliBody(ent);
}

void Body_HeliSmoke(edict_t *ent, float h)
{
	G3Body *b = Body_Of(ent);
	if (!b || b->family != BODY_HELI)
		return;
	IGhoulInst *inst = b->Inst();
	GhoulID eb = G3G_Part(inst, "smoke_engine"), tb = G3G_Part(inst, "smoke_tail");
	if (!eb || !tb)
		return;
	const char *fx = "environ/helismoke";
	if (h < 0) {
		G3FX_SlotRemove(ent, fx, eb);
		G3FX_SlotRemove(ent, fx, tb);
		return;
	}
	float es, ts;
	if (h < 0.25f) {
		es = 2.0f;
		ts = 1.0f;
	} else if (h < 0.5f) {
		es = 1.5f;
		ts = 0.75f;
	} else if (h < 0.75f) {
		es = 1.0f;
		ts = 0.5f;
		if (!G3FX_SlotHas(ent, fx, eb)) {
			G3FX_SlotAdd(ent, fx, eb, es);
			G3FX_SlotAdd(ent, fx, tb, ts);
			return;
		}
	} else
		return;
	G3FX_SlotRescale(ent, fx, eb, es);
	G3FX_SlotRescale(ent, fx, tb, ts);
}

void Body_HeliCanopyBlood(edict_t *ent, int variant)
{
	G3Body *b = Body_Of(ent);
	if (!b || g3locks.blood)
		return;
	IGhoulInst *canopy = G3G_ChildOnBolt(b->Inst(), G3G_Part(b->Inst(), "canopy"));
	if (!canopy)
		return;
	GhoulID m = G3G_Material(canopy, "canopy");
	if (!m)
		return;
	const char *s = variant == 1 ? "glass_blood_fnt" : (variant == 2 ? "glass_blood_bk" : (variant == 3 ? "glass_bloody" : NULL));
	if (!s) {
		canopy->RemoveSkin(m, 0, 0);
		return;
	}
	GhoulID sk = G3G_Skin(canopy, m, s);
	if (sk)
		canopy->SetSkin(m, sk, 0);
}

void Body_HeliBarrel(edict_t *ent, IGhoulInst *barrel, bool firing)
{
	(void)ent;
	if (!barrel)
		return;
	IGhoulObject *o = barrel->GetGhoulObject();
	if (!o)
		return;
	GhoulID s = o->FindSequence(firing ? "ghoul/enemy/chopper/barrel.ghl" : "ghoul/enemy/chopper/barrel_still.ghl");
	if (s)
		barrel->Play(s, G3_Time(), 0, true, firing ? kPlayLoop : kPlayOnce, false, false);
}

int Body_HeliCrewDead(edict_t *ent)
{
	G3Body *b = Body_Of(ent);
	if (!b || b->family != BODY_HELI)
		return 0;
	G3HeliBody *h = static_cast<G3HeliBody *>(b);
	return (h->gunnerDead ? 1 : 0) | (h->pilotDead ? 2 : 0);
}

void Body_HeliAllowCrewKill(edict_t *ent, bool allow)
{
	G3Body *b = Body_Of(ent);
	if (b && b->family == BODY_HELI)
		static_cast<G3HeliBody *>(b)->crewKillAllowed = allow;
}

// ---------------------------------------------------------------------------------------------------------------
// tank and snowcat
// ---------------------------------------------------------------------------------------------------------------
class G3TankBody : public G3Body {
public:
	bool snowcat;
	bool disabled, moving, alternate;
	int mgHealth;
	float params[5];
	float lastRic;
	void SaveFields(G3SaveBuf &b) override
	{
		b.B(disabled);
		b.B(moving);
		b.B(alternate);
		b.I(mgHealth, 4);
		for (int i = 0; i < 5; i++)
			b.F(params[i]);
		b.F(lastRic);
	}
	G3TankBody(edict_t *e, bool sc) : G3Body(e, sc ? BODY_SNOWCAT : BODY_TANK)
	{
		snowcat = sc;
		disabled = (e->spawnflags & 1) != 0;
		moving = false;
		alternate = false;
		mgHealth = 150;
		lastRic = -999;
		if (sc) {
			params[0] = 30;
			params[1] = 70;
			params[2] = 0.06f + 0.01f * Dmg_Toughness();
			params[3] = params[4] = 0;
		} else {
			params[0] = 20;
			params[1] = 50;
			params[2] = 0.04f;
			params[3] = 0.03f;
			params[4] = 0.05f;
		}
	}
	int Damage(G3Hit &h) override
	{
		Dmg_EnsureShot(ent, h);
		if (NoGhoulHits(this))
			return 0;
		if (AttackerWeapon(h, 1))
			return 0;
		const HitRecord &r = dmg_shot.rec[0];
		vec3_t at;
		G3_EntityToWorld(ent, r.point, at);
		const char *name = G3G_ModelName(r.inst ? r.inst->GetGhoulObject() : NULL);
		if (r.inst && (!Q_stricmp(name, "machine_gun") || (snowcat && !Q_stricmp(name, "gun"))) && mgHealth > 0) {
			mgHealth -= h.damage;
			if (mgHealth <= 0) {
				GhoulID b = G3G_Part(r.inst, "to_bolt_machine_gun");
				G3FX_OnBolt("environ/machgun_smoke2", ent, b, r.inst);
			}
		}
		if (!AttackerWeapon(h, 6))
			RicochetSparks(this, at, h, &lastRic);
		return h.damage;
	}
	void SetMoving(bool on)
	{
		moving = on;
		IGhoulInst *inst = Inst();
		const char *seq = snowcat ? "body" : "tank";
		GhoulID s = G3G_Sequence(inst, snowcat ? "enemy/snowcat" : "enemy/tank", seq);
		if (s)
			inst->Play(s, G3_Time(), 0, true, on ? kPlayLoop : kPlayOnce, false, false);
		if (snowcat) {
			GhoulID l = G3G_Part(inst, "snow_effect_left"), r = G3G_Part(inst, "snow_effect_right");
			if (on) {
				G3FX_SlotAdd(ent, "environ/snowspray", l, 1);
				G3FX_SlotAdd(ent, "environ/snowspray", r, 1);
			} else {
				G3FX_SlotRemove(ent, "environ/snowspray", l);
				G3FX_SlotRemove(ent, "environ/snowspray", r);
			}
		}
	}
	void SetAlternate(bool alt)
	{
		alternate = alt;
		if (!snowcat)
			return;
		IGhoulInst *inst = Inst();
		static const char *const wheels[] = {"front_left", "front_right", "rear_left", "rear_right"};
		for (const char *w : wheels)
			G3G_ShowPart(inst, G3G_Part(inst, w), !alt);
		const char *lights[2] = {"headlight_left", "headlight_right"};
		for (const char *l : lights) {
			IGhoulInst *c = G3G_ChildOnBolt(inst, G3G_Part(inst, l));
			if (c)
				c->SetAllPartsOnOff(!alt);
		}
		GhoulID el = G3G_Part(inst, "exhuast_left"), er = G3G_Part(inst, "exhuast_right");
		if (alt) {
			G3FX_SlotRemove(ent, "environ/kf_exhaust01", el);
			G3FX_SlotRemove(ent, "environ/kf_exhaust01", er);
		} else {
			G3FX_SlotAdd(ent, "environ/kf_exhaust01", el, 1);
			G3FX_SlotAdd(ent, "environ/kf_exhaust01", er, 1);
		}
	}
};

G3Body *G3_NewTankBody(edict_t *ent, bool snowcat)
{
	return new G3TankBody(ent, snowcat);
}

static G3TankBody *Tank(edict_t *ent)
{
	G3Body *b = Body_Of(ent);
	if (!b || (b->family != BODY_TANK && b->family != BODY_SNOWCAT))
		return NULL;
	return static_cast<G3TankBody *>(b);
}

void Body_VehicleMoving(edict_t *ent, bool on)
{
	if (G3TankBody *t = Tank(ent))
		t->SetMoving(on);
}

void Body_VehicleAlternate(edict_t *ent, bool alt)
{
	if (G3TankBody *t = Tank(ent))
		t->SetAlternate(alt);
}

bool Body_VehicleDisabled(edict_t *ent)
{
	G3TankBody *t = Tank(ent);
	return t && t->disabled;
}

void Body_VehicleSetDisabled(edict_t *ent, bool d)
{
	if (G3TankBody *t = Tank(ent))
		t->disabled = d;
}

int Body_VehicleMGHealth(edict_t *ent)
{
	G3TankBody *t = Tank(ent);
	return t ? t->mgHealth : 0;
}

void Body_VehicleAimParams(edict_t *ent, float out[5])
{
	G3TankBody *t = Tank(ent);
	for (int i = 0; i < 5; i++)
		out[i] = t ? t->params[i] : 0;
}
