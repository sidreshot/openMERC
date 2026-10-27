// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G3 unit tests - the test vectors of SPEC-720, SPEC-721 and SPEC-722 run against the G3 code with mock
// Ghoul models built from the user's game data (skipped when the fixtures are missing).
// Build and run: game\src\damage\test\build_tests.cmd
// Implements: SPEC-720, SPEC-721, SPEC-722 (test vectors)
#include "g3_test.h"
#include "../../effects/fx.h"

#include <cstdio>
#include <cstdlib>

using namespace mock;

static int failures, checks;
extern int test_clientDamaged;
#define CHECK(cond, ...)                                                                                             \
	do {                                                                                                             \
		checks++;                                                                                                    \
		if (!(cond)) {                                                                                               \
			failures++;                                                                                              \
			printf("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);                                                  \
			printf(__VA_ARGS__);                                                                                     \
			printf("\n");                                                                                            \
		}                                                                                                            \
	} while (0)

static const char *fixtures = "C:\\openmerc-data\\g3\\mock\\";
static edict_t edicts[64];
static gclient_t clients[4];
static Inventory invs[4];

// ---------------------------------------------------------------------------------------------------------------
// scene helpers
// ---------------------------------------------------------------------------------------------------------------
class TestOwner : public G3BodyOwner {
public:
	int deaths = 0, pains = 0, hits = 0;
	int lastPainDamage = 0;
	void Die(edict_t *, edict_t *, int, const vec3_t, int) override { deaths++; }
	void Pain(edict_t *, const vec3_t, int, int damage) override
	{
		pains++;
		lastPainDamage = damage;
	}
	void OnHit(void) override { hits++; }
};
static TestOwner owners[16];
static int numOwners;

static void Reset(void)
{
	memset(edicts, 0, sizeof edicts);
	memset(clients, 0, sizeof clients);
	g_edicts = edicts;
	world = edicts;
	globals.edicts = edicts;
	globals.max_edicts = 64;
	globals.num_edicts = 5;
	game.maxclients = 4;
	edicts[0].inuse = true;
	edicts[0].classname = (char *)"worldspawn";
	level.time = 10;
	strcpy(level.mapname, "irq1a");
	test::cvars.clear();
	test::cvars["ai_maxcorpses"] = 5;
	test::log.clear();
	test::irandQueue.clear();
	test::traceTarget = nullptr;
	test_sameTeam = false;
	nextShot.clear();
	numOwners = 0;
	memset(&g3hooks, 0, sizeof g3hooks);
	Gore_ReadLocks();
	Gore_LevelStart();
	fx::LevelReset(); // G7's game-wide effect filters (as at a level start)
	for (int i = 0; i < 16; i++)
		owners[i] = TestOwner();
}

static const Model *FindLib(const char *name)
{
	for (auto &m : test::lib)
		if (Lower(m.name) == Lower(name) && Lower(m.dir) != "enemy/bolt")
			return &m;
	return nullptr;
}

static edict_t *Character(const char *set, int family, const char *classname, int health, const vec3_t org,
                          float yaw)
{
	const Model *m = FindLib(set);
	if (!m)
		return nullptr;
	edict_t *e = G_Spawn();
	Object *o = new Object();
	o->m = *m;
	e->ghoulInst = o->CreateInstance();
	e->ghoulInst->SetUserValue(e);
	e->classname = (char *)classname;
	e->health = e->max_health = health;
	e->takedamage = DAMAGE_YES;
	e->movetype = MOVETYPE_STEP;
	e->mass = 200;
	e->solid = SOLID_BBOX;
	VectorCopy(org, e->s.origin);
	e->s.angles[YAW] = yaw;
	VectorSet(e->mins, -8, -8, -32);
	VectorSet(e->maxs, 8, 8, 41);
	VectorAdd(e->s.origin, e->mins, e->absmin);
	VectorAdd(e->s.origin, e->maxs, e->absmax);
	G3Body *b = Body_Create(e, family);
	b->owner = &owners[numOwners++];
	if (G3_IsHumanFamily(family))
		b->SetRightHand(6);
	b->Precache();
	return e;
}

static edict_t *Player(int slot, const vec3_t org, float yaw)
{
	edict_t *e = &edicts[slot];
	memset(e, 0, sizeof *e);
	e->inuse = true;
	e->s.number = slot;
	e->client = &clients[slot - 1];
	invs[slot - 1] = Inventory();
	e->client->inv = &invs[slot - 1];
	e->health = e->max_health = 100;
	e->takedamage = DAMAGE_AIM;
	e->movetype = MOVETYPE_WALK;
	e->mass = 200;
	e->viewheight = 22;
	e->classname = (char *)"player";
	VectorCopy(org, e->s.origin);
	e->s.angles[YAW] = yaw;
	e->client->v_angle[YAW] = yaw;
	VectorSet(e->mins, -16, -16, -24);
	VectorSet(e->maxs, 16, 16, 32);
	return e;
}

static int ZoneOwnerPart(edict_t *e, int z, char *name)
{
	G3Body *b = Body_Of(e);
	GhoulID p = b->gore->zones[z].owner;
	static_cast<Inst *>(e->ghoulInst)->obj->GetPartName(p, name);
	return p;
}

// a traced shot from `from` (eye height `h`) along +x/-x into `target`; records = (part, distance) on the target
// A single record on the character's own instance is completed with its exit record on the same part 16 units on
// (a bullet entering and leaving one mesh): SPEC-AMEND-005 G3.4 gives a lone record no hit zone (see T720_g3_4).
static void Shoot(edict_t *attacker, edict_t *target, const vec3_t start, const vec3_t end,
                  std::vector<ShotRec> recs, int damage, int dflags, int mod)
{
	if (recs.size() == 1 && recs[0].model.empty())
		recs.push_back({"", recs[0].part, recs[0].distance + 16});
	nextShot = recs;
	test::traceTarget = target;
	trace_t tr = Dmg_ShotTrace(start, nullptr, nullptr, end, attacker, MASK_SHOT);
	vec3_t dir;
	VectorSubtract(end, start, dir);
	VectorNormalize(dir);
	Dmg_Damage(target, attacker, attacker, dir, tr.endpos, start, damage, damage, dflags, mod);
	nextShot.clear();
	test::traceTarget = nullptr;
}

static int Count(const char *prefix)
{
	int n = 0;
	for (auto &l : test::log)
		if (l.compare(0, strlen(prefix), prefix) == 0)
			n++;
	return n;
}

static void DumpLog(void)
{
	for (auto &l : test::log)
		printf("    | %s\n", l.c_str());
}

// shooter at (-200, 0, z) firing along +x at a target at the origin facing the shooter (yaw 180)
static void Front(float z, vec3_t s, vec3_t e)
{
	VectorSet(s, -200, 0, z);
	VectorSet(e, 200, 0, z);
}

// ---------------------------------------------------------------------------------------------------------------
// SPEC-720 test vectors
// ---------------------------------------------------------------------------------------------------------------
static void T720_1(void)
{
	printf("SPEC-720 TV1 toughness / nodamage\n");
	for (int nd = 0; nd <= 2; nd++) {
		Reset();
		test::cvars["sk_toughness"] = 2;
		test::cvars["nodamage"] = (float)nd;
		vec3_t po = {0, 0, 0}, co = {-200, 0, 0};
		edict_t *p = Player(1, po, 0);
		edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 0);
		vec3_t dir = {1, 0, 0}, pt = {0, 0, 10};
		Dmg_Damage(p, c, c, dir, pt, c->s.origin, 20, 20, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
		int expect[3] = {3, 1, 0};
		CHECK(p->health == 100 - expect[nd], "nodamage %d: health %d expected %d", nd, p->health, 100 - expect[nd]);
	}
}

static void T720_2(void)
{
	printf("SPEC-720 TV2 sneak attack, chest and head multipliers, overkill cap\n");
	char chest[64], head[64], headb[64];
	for (int aware = 0; aware <= 1; aware++) {
		Reset();
		vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
		edict_t *p = Player(1, po, 0);
		edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
		if (aware)
			c->enemy = p;
		ZoneOwnerPart(c, GZ_CHEST_FRONT, chest);
		Front(15, s, e);
		Shoot(p, c, s, e, {{"", chest, 190}, {"", chest, 210}}, 40, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
		int expect = aware ? 60 : 20;
		CHECK(c->health == expect, "aware=%d chest(%s): health %d expected %d", aware, chest, c->health, expect);
	}
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	ZoneOwnerPart(c, GZ_HEAD_FRONT, head);
	ZoneOwnerPart(c, GZ_HEAD_BACK, headb);
	Front(32, s, e);
	Shoot(p, c, s, e, {{"", head, 192}, {"", headb, 208}}, 40, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	CHECK(c->health == -150, "head(%s/%s): health %d expected -150", head, headb, c->health);
	CHECK(!Body_Of(c)->gibbed, "no gib without EXPLOSIVE");
	CHECK(owners[0].deaths == 1, "death dispatched (%d)", owners[0].deaths);
	CHECK(Count("effect:gore/bloodcloud") >= 1, "exit wound blood cloud");
}

static void T720_3(void)
{
	printf("SPEC-720 TV3 arm hit and limb injury duration\n");
	Reset();
	test::cvars["sk_toughness"] = 1;
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	char arm[64];
	ZoneOwnerPart(c, GZ_ARM_UPPER_RIGHT, arm);
	Front(22, s, e);
	Shoot(p, c, s, e, {{"", arm, 195}}, 40, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	CHECK(c->health == 80, "arm(%s): health %d expected 80", arm, c->health);
	G3Body *b = Body_Of(c);
	int inj = b->LimbInjury();
	CHECK(inj & LIMB_RARM, "right arm hurt (bits %d)", inj);
	level.time += 3.7f;
	CHECK(b->LimbInjury() & LIMB_RARM, "still hurt after 3.7 s");
	level.time += 1.6f;
	CHECK(!(b->LimbInjury() & LIMB_RARM), "healed after 5.3 s");
}

static void T720_4(void)
{
	printf("SPEC-720 TV4 zone armour (configuration 1)\n");
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	G3Body *b = Body_Of(c);
	b->SetArmour(1);
	CHECK(b->ArmourPercent() == 100, "armour percent %d", b->ArmourPercent());
	char chest[64];
	ZoneOwnerPart(c, GZ_CHEST_FRONT, chest);
	Front(15, s, e);
	int i;
	for (i = 1; i <= 7; i++)
		Shoot(p, c, s, e, {{"", chest, 190}}, 40, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	CHECK(c->health == 100, "7 absorbed hits: health %d", c->health);
	CHECK(Count("effect:spark2") == 7, "7 armour sparks (%d)", Count("effect:spark2"));
	CHECK(Count("sound:impact/armor/hit.wav") == 7, "7 armour sounds");
	Inst *in = static_cast<Inst *>(c->ghoulInst);
	GhoulID wm = b->gore->zones[GZ_CHEST_FRONT].woundMesh;
	CHECK(in->IsPartOn(wm) && in->SkinOn("gz", wm) == "gz_armor", "chest wound mesh shows gz_armor (%s)",
	      in->SkinOn("gz", wm).c_str());
	Shoot(p, c, s, e, {{"", chest, 190}}, 40, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	CHECK(c->health == 80, "8th hit passes 20: health %d", c->health);
}

static void T720_5(void)
{
	printf("SPEC-720 TV5 grenade min/max and rocket gib\n");
	Reset();
	vec3_t co = {0, 0, 0};
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = &edicts[1];
	edict_t *gren = G_Spawn();
	vec3_t centre = {-100, 0, 4.5f};
	VectorCopy(centre, gren->s.origin);
	Dmg_RadiusMinMax(gren, gren, centre, 200, 40, 200, nullptr, MOD_GRENADE, DMGF_EXPLOSIVE);
	CHECK(c->health == -20, "grenade at 100 units: health %d expected -20", c->health);
	CHECK(!Body_Of(c)->gibbed, "no gib at -20");
	Reset();
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = &edicts[1];
	vec3_t dir = {1, 0, 0}, pt = {-8, 0, 10}, org = {-200, 0, 10};
	Dmg_Damage(c, &edicts[0], &edicts[0], dir, pt, org, 250, 250, DMGF_EXPLOSIVE, MOD_ROCKET);
	CHECK(c->health == -150, "rocket: health %d expected -150", c->health);
	CHECK(Body_Of(c)->gibbed, "rocket gibs");
	CHECK(Count("effect:gore/hugeburst2") == 1, "one hugeburst2 (%d)", Count("effect:gore/hugeburst2"));
	CHECK(Count("effect:gore/gibbody") == 1, "full gib effect");
}

static void T720_6(void)
{
	printf("SPEC-720 TV6 back-stab, slash zones\n");
	Reset();
	vec3_t po = {-40, 0, 0}, co = {0, 0, 0}, s = {-40, 0, 15}, e = {40, 0, 15};
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 0); // facing away
	c->enemy = p;
	G3_ResetHint();
	Shoot(p, c, s, e, {}, 22, DMGF_SLASH | DMGF_TRACED, MOD_KNIFE_SLASH);
	CHECK(c->health == 45, "back-stab 22 x 2.5 = 55: health %d", c->health);
	// a slash never lands on zones 0-2 or 20-25 with estimated zones: force the head draw (n = 6 above origin)
	int bad = 0;
	for (int k = 0; k < 20; k++) {
		Reset();
		p = Player(1, po, 0);
		c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 1000, co, 0);
		c->enemy = p;
		G3_ResetHint();
		Shoot(p, c, s, e, {}, 10, DMGF_SLASH | DMGF_TRACED, MOD_KNIFE_SLASH);
		G3Body *b = Body_Of(c);
		for (int z = 0; z < 26; z++)
			if ((z <= 2 || (z >= 20 && z <= 23)) && b->gore->zones[z].damage != 0) // 24/25 are wound links of 13/14
				bad++;
	}
	CHECK(bad == 0, "slash wounded a head/leg zone %d times", bad);
}

static void T720_7(void)
{
	printf("SPEC-720 TV7 team play without friendly fire\n");
	Reset();
	test::cvars["deathmatch"] = 1;
	test::cvars["dmflags"] = 0x80 | 0x100;
	test_sameTeam = true;
	vec3_t a = {-200, 0, 0}, b = {0, 0, 0};
	edict_t *p1 = Player(1, a, 0), *p2 = Player(2, b, 180);
	vec3_t dir = {1, 0, 0}, pt = {0, 0, 0};
	Dmg_Damage(p2, p1, p1, dir, pt, a, 100, 100, DMGF_EXPLOSIVE, MOD_ROCKET);
	CHECK(p2->health == 100, "teammate rocket: health %d", p2->health);
	CHECK(VectorLength(p2->velocity) > 0, "but pushes (%f)", VectorLength(p2->velocity));
	test::cvars["dmflags"] = 0x80;
	Dmg_Damage(p2, p1, p1, dir, pt, a, 10, 10, DMGF_EXPLOSIVE, MOD_ROCKET);
	CHECK(p2->health == 90 && (dmg_lastMod & DMG_MOD_FRIENDLY), "friendly fire allowed: health %d mod %x",
	      p2->health, dmg_lastMod);
}

static void T720_8(void)
{
	printf("SPEC-720 TV8 hit direction\n");
	Reset();
	test_clientDamaged = 0;
	vec3_t o = {0, 0, 0}, right = {0, -100, 0}, front = {100, 0, 0};
	edict_t *p = Player(1, o, 0);
	edict_t *a = Player(2, right, 90);
	vec3_t dir = {0, 1, 0}, pt = {0, -10, 10};
	Dmg_Damage(p, a, a, dir, pt, right, 5, 0, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(p->client->damage_dir == 4, "from the right: %d expected 4", p->client->damage_dir);
	VectorCopy(front, a->s.origin);
	vec3_t dir2 = {-1, 0, 0}, pt2 = {10, 0, 10};
	Dmg_Damage(p, a, a, dir2, pt2, front, 5, 0, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(p->client->damage_dir == 0x10, "from the front: %d expected 0x10", p->client->damage_dir);
	CHECK(Count("sound:impact/player/hit") == 2, "player hit sounds");
	CHECK(test_clientDamaged == 2, "remote-camera hook called for each client hit (%d)", test_clientDamaged);
}

// ---------------------------------------------------------------------------------------------------------------
// SPEC-721 test vectors
// ---------------------------------------------------------------------------------------------------------------
static void T721_1(void)
{
	printf("SPEC-721 TV1 wound growth\n");
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 1000, co, 180);
	c->enemy = p;
	G3Body *b = Body_Of(c);
	Inst *in = static_cast<Inst *>(c->ghoulInst);
	char chest[64];
	ZoneOwnerPart(c, GZ_CHEST_FRONT, chest);
	GhoulID wm = b->gore->zones[GZ_CHEST_FRONT].woundMesh;
	CHECK(wm && !in->IsPartOn(wm), "chest wound mesh hidden before");
	Front(15, s, e);
	Shoot(p, c, s, e, {{"", chest, 190}}, 20, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	std::string s1 = in->SkinOn("gz", wm);
	CHECK(in->IsPartOn(wm) && s1.compare(0, 7, "gz_sing") == 0, "level 1 skin %s", s1.c_str());
	for (int i = 0; i < 5; i++)
		Shoot(p, c, s, e, {{"", chest, 190}}, 20, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	std::string s2 = in->SkinOn("gz", wm);
	CHECK(s2.compare(0, 8, "gz_multi") == 0 && s2.back() == s1.back(), "level 2 skin %s (was %s)", s2.c_str(),
	      s1.c_str());
	for (int i = 0; i < 5; i++)
		Shoot(p, c, s, e, {{"", chest, 190}}, 20, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	std::string s3 = in->SkinOn("gz", wm);
	CHECK(s3.compare(0, 7, "gz_mass") == 0 && s3.back() == s1.back(), "level 3 skin %s", s3.c_str());
	CHECK(b->gore->zones[GZ_CHEST_FRONT].damage == 220, "accumulated %d", b->gore->zones[GZ_CHEST_FRONT].damage);
	// knife slash selects the _k skins
	Reset();
	p = Player(1, po, 0);
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 1000, co, 180);
	c->enemy = p;
	in = static_cast<Inst *>(c->ghoulInst);
	b = Body_Of(c);
	wm = b->gore->zones[GZ_CHEST_FRONT].woundMesh;
	Shoot(p, c, s, e, {{"", chest, 190}}, 20, DMGF_PROJECTILE, MOD_KNIFE_SLASH);
	CHECK(in->SkinOn("gz", wm) == "gz_sing_k", "knife skin %s", in->SkinOn("gz", wm).c_str());
	// lock_gorezones: nothing visible
	Reset();
	test::cvars["lock_gorezones"] = 1;
	Gore_ReadLocks();
	p = Player(1, po, 0);
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 1000, co, 180);
	c->enemy = p;
	in = static_cast<Inst *>(c->ghoulInst);
	wm = Body_Of(c)->gore->zones[GZ_CHEST_FRONT].woundMesh;
	Shoot(p, c, s, e, {{"", chest, 190}}, 20, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	CHECK(!in->IsPartOn(wm), "lock_gorezones: wound hidden");
	test::cvars["lock_gorezones"] = 0;
	Gore_ReadLocks();
}

static void T721_2(void)
{
	printf("SPEC-721 TV2 accumulation capped by health\n");
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	char chest[64];
	ZoneOwnerPart(c, GZ_CHEST_FRONT, chest);
	Front(15, s, e);
	Shoot(p, c, s, e, {{"", chest, 190}}, 500, DMGF_PROJECTILE, MOD_SNIPERRIFLE);
	G3Zone &z = Body_Of(c)->gore->zones[GZ_CHEST_FRONT];
	CHECK(z.damage == 100 && z.level == 1, "zone damage %d level %d", z.damage, z.level);
}

static void T721_3(void)
{
	printf("SPEC-721 TV3 severing by bullets on a corpse\n");
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	c->health = -10;
	G3Body *b = Body_Of(c);
	Inst *in = static_cast<Inst *>(c->ghoulInst);
	char arm[64];
	ZoneOwnerPart(c, GZ_ARM_LOWER_LEFT, arm);
	G3Zone &z = b->gore->zones[GZ_ARM_LOWER_LEFT];
	Front(3, s, e);
	int hits = 0;
	while (in->IsPartOn(z.owner) && hits < 100) {
		Shoot(p, c, s, e, {{"", arm, 195}}, 20, DMGF_PROJECTILE, MOD_PISTOL1);
		hits++;
	}
	CHECK(hits == 51, "severed after %d hits (expected 51: corpse hits add 10 each)", hits);
	char cap[64];
	in->obj->GetPartName(z.bodyCap, cap);
	CHECK(in->IsPartOn(z.bodyCap) && in->SkinOn("c", z.bodyCap) == "c_blood", "body cap %s shown with c_blood (%s)",
	      cap, in->SkinOn("c", z.bodyCap).c_str());
	CHECK(in->ChildByModel("g_bone_anim1") != nullptr, "g_bone_anim1 on the stump");
	// SPEC-AMEND-006 G3.16: both cap bolt parts of the handed-over zone are off on the body, on on the piece
	CHECK((!z.bodyCapBolt || !in->IsPartOn(z.bodyCapBolt)) && (!z.pieceCapBolt || !in->IsPartOn(z.pieceCapBolt)),
	      "G3.16 cap bolt parts off on the body");
	int pieces = 0;
	for (int i = 1; i < globals.num_edicts; i++)
		if (edicts[i].inuse && (edicts[i].g3flags & G3F_PIECE)) {
			pieces++;
			Inst *pi = static_cast<Inst *>(edicts[i].ghoulInst);
			CHECK((!z.pieceCapBolt || pi->IsPartOn(z.pieceCapBolt)) && (!z.bodyCapBolt || pi->IsPartOn(z.bodyCapBolt)),
			      "G3.16 cap bolt parts on on the piece");
			CHECK(pi->IsPartOn(pi->obj->FindPart(arm)), "piece shows %s", arm);
			CHECK(pi->ChildByModel("g_bone_anim1") != nullptr, "piece has its cap bolt-on");
		}
	CHECK(pieces == 1, "one severed piece (%d)", pieces);
	// lock_sever: never
	Reset();
	test::cvars["lock_sever"] = 1;
	Gore_ReadLocks();
	p = Player(1, po, 0);
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->health = -10;
	in = static_cast<Inst *>(c->ghoulInst);
	GhoulID own = Body_Of(c)->gore->zones[GZ_ARM_LOWER_LEFT].owner;
	for (int i = 0; i < 80; i++)
		Shoot(p, c, s, e, {{"", arm, 195}}, 20, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(in->IsPartOn(own), "lock_sever: forearm stays");
	test::cvars["lock_sever"] = 0;
	Gore_ReadLocks();
}

static void T721_4(void)
{
	printf("SPEC-721 TV4 head explosion (ai_goretest 1)\n");
	Reset();
	test::cvars["ai_goretest"] = 1;
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	Inst *in = static_cast<Inst *>(c->ghoulInst);
	char head[64], headb[64];
	ZoneOwnerPart(c, GZ_HEAD_FRONT, head);
	ZoneOwnerPart(c, GZ_HEAD_BACK, headb);
	Front(32, s, e);
	Shoot(p, c, s, e, {{"", head, 192}, {"", headb, 208}}, 40, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	CHECK(!in->Shown(head) && !in->Shown(headb), "head meshes hidden");
	CHECK(Count("sound:impact/gore/headexp.wav") == 1, "headexp sound");
	CHECK(Count("te:18 count") >= 1, "flesh debris");
	CHECK(Count("effect:gore/headcloud") == 1, "headcloud");
	CHECK(in->Shown("_Cap_neck_head"), "neck cap shown");
	CHECK(in->ChildByModel("g_neck_torn_anim2") || in->ChildByModel("g_half_head"), "neck cap bolt-on");
	if (failures)
		DumpLog();
}

static void T721_5(void)
{
	printf("SPEC-721 TV5 brain shot (ecto: the model with _headhelmet)\n");
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	invs[0].weapon = 6;
	edict_t *c = Character("commander", BODY_ECTO, "m_irq_eofficer", 100, co, 180);
	if (!c) {
		printf("  skipped (no fixture)\n");
		return;
	}
	c->enemy = p;
	Inst *in = static_cast<Inst *>(c->ghoulInst);
	char head[64], headb[64];
	ZoneOwnerPart(c, GZ_HEAD_FRONT, head);
	ZoneOwnerPart(c, GZ_HEAD_BACK, headb);
	Front(32, s, e);
	Shoot(p, c, s, e, {{"", head, 195}, {"", headb, 205}}, 100, DMGF_PROJECTILE, MOD_SNIPERRIFLE);
	CHECK(in->Shown("_headhelmet") && !in->Shown(head), "open skull shown (%s hidden)", head);
	CHECK(in->ChildByModel("g_brain") != nullptr, "g_brain on abolt_head_t");
	Inst *br = in->ChildByModel("g_brain");
	CHECK(br && fabsf(br->xf.m[0][0] - 1.17f) < 0.001f, "brain scale 1.17");
}

static void T721_6(void)
{
	printf("SPEC-721 TV6 one grenade, three gibs\n");
	Reset();
	vec3_t o1 = {0, 0, 0}, o2 = {0, 20, 0}, o3 = {0, -20, 0};
	edict_t *c[3] = {Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, o1, 180),
		Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, o2, 180),
		Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, o3, 180)};
	for (auto *x : c)
		x->enemy = &edicts[1];
	edict_t *gren = G_Spawn();
	vec3_t centre = {-30, 0, 4.5f};
	VectorCopy(centre, gren->s.origin);
	Dmg_RadiusMinMax(gren, gren, centre, 1000, 1000, 300, nullptr, MOD_GRENADE, DMGF_EXPLOSIVE);
	int gibbed = 0;
	for (auto *x : c)
		gibbed += Body_Of(x)->gibbed;
	CHECK(gibbed == 3, "three bodies blown apart (%d)", gibbed);
	CHECK(Count("effect:gore/hugeburst2") == 3, "hugeburst2 on at most three (%d)", Count("effect:gore/hugeburst2"));
	int pieces[3] = {0, 0, 0};
	for (int i = 1; i < globals.num_edicts; i++)
		if (edicts[i].inuse && (edicts[i].g3flags & G3F_PIECE))
			for (int k = 0; k < 3; k++)
				if (edicts[i].owner == c[k])
					pieces[k]++;
	CHECK(pieces[0] <= 2 && pieces[1] <= 2 && pieces[2] == 0, "flying pieces %d %d %d", pieces[0], pieces[1],
	      pieces[2]);
	Reset();
	test::cvars["lock_sever"] = 1;
	Gore_ReadLocks();
	edict_t *d = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, o1, 180);
	VectorCopy(centre, gren->s.origin);
	gren = G_Spawn();
	VectorCopy(centre, gren->s.origin);
	Dmg_RadiusMinMax(gren, gren, centre, 1000, 1000, 300, nullptr, MOD_GRENADE, DMGF_EXPLOSIVE);
	CHECK(!Body_Of(d)->gibbed && d->health < 0, "lock_sever: not blown apart");
	test::cvars["lock_sever"] = 0;
	Gore_ReadLocks();
}

static void T721_7(void)
{
	printf("SPEC-721 TV7 microwave gib\n");
	Reset();
	vec3_t co = {0, 0, 0};
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = &edicts[1];
	c->health = -10;
	vec3_t dir = {1, 0, 0}, pt = {-8, 0, 10}, org = {-200, 0, 10};
	Dmg_Damage(c, &edicts[0], &edicts[0], dir, pt, org, 1000, 0, DMGF_MICROWAVE | DMGF_EXPLOSIVE, MOD_MPG);
	Inst *in = static_cast<Inst *>(c->ghoulInst);
	CHECK(Body_Of(c)->gibbed, "full gib");
	CHECK(Count("effect:gore/mpgsplatter") == 1 && Count("effect:gore/hugeburst2") == 0, "mpgsplatter instead");
	CHECK(fabsf(in->tint[1] - 0.3f) < 0.01f && in->tint[2] == 0, "tinted (1, 0.3, 0)");
}

static void T721_10(void)
{
	printf("SPEC-721 TV10 ZGIA savegame chunk\n");
	Reset();
	vec3_t co = {0, 0, 0};
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	Body_Of(c)->gore->zones[0].damage = 42;
	Body_Of(c)->gore->SaveWrite();
	CHECK(Count("chunk:4149475a len=104 first=42") == 1, "ZGIA chunk with 26 ints");
	if (Count("chunk:") && !Count("chunk:4149475a"))
		DumpLog();
}

// ---------------------------------------------------------------------------------------------------------------
// SPEC-722
// ---------------------------------------------------------------------------------------------------------------
static void T722_deaths(void)
{
	printf("SPEC-722 death choices\n");
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	G3Body *b = Body_Of(c);
	b->SetAnim(Anim_Human("std_istand_n_a_n"), true);
	char foot[64];
	ZoneOwnerPart(c, GZ_FOOT_RIGHT, foot);
	Front(-31, s, e);
	Shoot(p, c, s, e, {{"", foot, 195}}, 300, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(c->health <= 0, "killed (%d)", c->health);
	const G3Anim *a = b->ChooseDeath(p, p, 150, DMGF_PROJECTILE, nullptr);
	CHECK(a && !Q_stricmp(a->name, "std_slegrt_2bk_n_n"), "right foot death: %s", a ? a->name : "none");
	// lock_deaths
	Reset();
	test::cvars["lock_deaths"] = 1;
	Gore_ReadLocks();
	p = Player(1, po, 0);
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	b = Body_Of(c);
	b->SetAnim(Anim_Human("std_istand_n_a_n"), true);
	c->health = -5;
	a = b->ChooseDeath(p, p, 150, DMGF_PROJECTILE, nullptr);
	CHECK(a && !Q_stricmp(a->name, "std_shead_2frnt_n_n"), "lock_deaths: %s", a ? a->name : "none");
	test::cvars["lock_deaths"] = 0;
	Gore_ReadLocks();
}

static int armourDropped;
static edict_t *DropArmour(edict_t *, int count)
{
	armourDropped = count;
	return nullptr;
}

static void T722_armour(void)
{
	printf("SPEC-722 TV7 armour pickup when the death animation ends\n");
	Reset();
	armourDropped = -1;
	g3hooks.dropArmour = DropArmour;
	vec3_t co = {0, 0, 0};
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	G3Body *b = Body_Of(c);
	b->SetArmour(1);
	c->health = -10;
	b->SetAnim(Anim_Human("std_stoknees_2bk_n_n"), true);
	b->finished = true;
	b->Think();
	CHECK(armourDropped >= 50, "armour pickup count %d", armourDropped);
	CHECK(b->ArmourPercent() == 0, "armour cleared");
	// armour types 4/5 scale health
	Reset();
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	Body_Of(c)->SetArmour(5);
	Body_Of(c)->SetArmour(3);
	CHECK(c->health == 200 && c->max_health == 200, "armour 5: health %d", c->health);
	CHECK(Body_Of(c)->gore->zones[0].damage == -1200 && Body_Of(c)->gore->zones[25].damage == -1200,
	      "armour 3: 4 x 300 on every zone (%d)", Body_Of(c)->gore->zones[0].damage);
}

static void T722_expression(void)
{
	printf("SPEC-722 expression state machine\n");
	Reset();
	vec3_t co = {0, 0, 0};
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	G3Body *b = Body_Of(c);
	b->SetExpression(EXPR_PAIN, 10, false);
	b->SetExpression(EXPR_AFRAID, 5, false);
	CHECK(b->expr == EXPR_PAIN, "pain is only replaced by dead (%d)", b->expr);
	b->SetExpression(EXPR_DEAD, 1, false);
	CHECK(b->expr == EXPR_DEAD, "dead accepted");
	level.time += 2;
	b->Think();
	CHECK(b->expr == EXPR_NEUTRAL, "expired back to neutral (%d)", b->expr);
}

// SPEC-012 §6 mode 1 / SPEC-722 root motion: the yaw rate is the rotation of Δ, not an element of Δ·XF·s
static void T722_rootyaw(void)
{
	printf("SPEC-722 root-motion yaw rate from the quake_origin rate matrix\n");
	Reset();
	vec3_t co = {0, 0, 0};
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	G3Body *b = Body_Of(c);
	if (!b || !b->Inst()) {
		printf("  skipped (no fixture)\n");
		return;
	}
	// instance transform: model X -> entity +Y (the roster's -90 degree transform), scale 0.9
	const float sc = 0.9f;
	Matrix4 xf;
	memset(&xf, 0, sizeof xf);
	xf.m[0][1] = sc;
	xf.m[1][0] = -sc;
	xf.m[2][2] = sc;
	xf.m[3][3] = 1;
	b->Inst()->SetTransform(&xf);
	const float D = 1.2f, s = 1.0f / (0.001f * D);
	for (float theta : {0.0f, 0.0005f, -0.0012f}) {
		// Δ = rotation by theta about z (row-vector convention), M = Δ·XF·s
		float c0 = cosf(theta), s0 = sinf(theta);
		float d[3][3] = {{c0, s0, 0}, {-s0, c0, 0}, {0, 0, 1}};
		Matrix4 m;
		memset(&m, 0, sizeof m);
		for (int r = 0; r < 3; r++)
			for (int k = 0; k < 3; k++) {
				float v = 0;
				for (int j = 0; j < 3; j++)
					v += d[r][j] * xf.m[j][k];
				m.m[r][k] = v * s;
			}
		m.m[3][3] = 1;
		float rate = b->RootYawRate(&m);
		CHECK(fabsf(rate - theta * s) < 0.02f + 0.01f * fabsf(theta * s), "theta %f: rate %f, want %f", theta, rate,
		      theta * s);
	}
}

// one traced shot with exactly these records (no completion of a single record, unlike Shoot)
static void ShootRaw(edict_t *attacker, edict_t *target, const vec3_t start, const vec3_t end,
                     std::vector<ShotRec> recs, int damage, int dflags, int mod)
{
	nextShot = recs;
	test::traceTarget = target;
	trace_t tr = Dmg_ShotTrace(start, nullptr, nullptr, end, attacker, MASK_SHOT);
	vec3_t dir;
	VectorSubtract(end, start, dir);
	VectorNormalize(dir);
	Dmg_Damage(target, attacker, attacker, dir, tr.endpos, start, damage, damage, dflags, mod);
	nextShot.clear();
	test::traceTarget = nullptr;
}

static int BoltOfEffect(const char *effect)
{
	std::string pre = std::string("effect:") + effect + " ";
	for (auto &l : test::log)
		if (l.compare(0, pre.size(), pre) == 0) {
			size_t k = l.find("/bolt");
			if (k != std::string::npos)
				return atoi(l.c_str() + k + 5);
		}
	return -1;
}

// SPEC-AMEND-005 items G3.1-G3.7, G3.11, G3.14
static void T_A005(void)
{
	printf("SPEC-AMEND-005 G3.1-G3.7, G3.11, G3.14\n");
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	char part[64], part2[64];

	// G3.3: a lone record on a shown wound mesh belongs to no zone: unscaled damage, no wound
	Reset();
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	G3Body *b = Body_Of(c);
	Inst *in = static_cast<Inst *>(c->ghoulInst);
	c->enemy = p; // aware: no sneak-attack doubling (TV2)
	G3Zone &h0 = b->gore->zones[GZ_HEAD_FRONT];
	in->obj->GetPartName(h0.woundMesh, part);
	in->SetPartOnOff(h0.woundMesh, true);
	Front(36, s, e);
	ShootRaw(p, c, s, e, {{"", part, 192}, {"", part, 200}}, 20, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(c->health == 80, "G3.3 wound-mesh records: no zone, unscaled (health %d)", c->health);
	CHECK(h0.damage == 0, "G3.3 no wound on the head zone (%d)", h0.damage);

	// G3.4: a single zone-mapping record (the exit record) gives no hit zone
	Reset();
	p = Player(1, po, 0);
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	b = Body_Of(c);
	ZoneOwnerPart(c, GZ_HEAD_FRONT, part);
	ShootRaw(p, c, s, e, {{"", part, 192}}, 20, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(c->health == 80, "G3.4 lone record: unscaled head hit (health %d)", c->health);
	CHECK(b->gore->zones[GZ_HEAD_FRONT].damage == 0 && b->gore->zones[GZ_HEAD_BACK].damage == 0,
	      "G3.4 no wound");
	CHECK(Count("te:3 ") == 0, "G3.4 no blood point, no splat");

	// G3.5: two candidates without gore bolt (male thighs): the lower zone index
	Reset();
	p = Player(1, po, 0);
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	b = Body_Of(c);
	ZoneOwnerPart(c, GZ_LEG_UPPER_RIGHT_FRONT, part);
	ZoneOwnerPart(c, GZ_LEG_UPPER_RIGHT_BACK, part2);
	CHECK(!strcmp(part, part2), "male thighs share the owner %s / %s", part, part2);
	Front(-10, s, e);
	Shoot(p, c, s, e, {{"", part, 195}}, 20, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(b->gore->lastZone == GZ_LEG_UPPER_RIGHT_FRONT, "G3.5 thigh record -> zone 16 (%d)", b->gore->lastZone);

	// G3.7: the per-hit puff; G3.6: the blood splat from the clamped damage point
	CHECK(Count("te:18 count=") == 1, "G3.7 one TE 18 puff");
	for (auto &l : test::log)
		if (l.compare(0, 12, "te:18 count=") == 0) {
			int n = atoi(l.c_str() + 12);
			CHECK(n >= 0 && n <= 5 && l.find("size=4 chunk=32 mat=35") != std::string::npos, "G3.7 payload %s",
			      l.c_str());
		}

	// G3.1: killing chest shot: cloud on the gore bolt, pool tracking on the body-side cap bolt, killing zone 7
	Reset();
	p = Player(1, po, 0);
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	b = Body_Of(c);
	ZoneOwnerPart(c, GZ_CHEST_FRONT, part);
	Front(15, s, e);
	Shoot(p, c, s, e, {{"", part, 190}}, 200, DMGF_PROJECTILE, MOD_PISTOL1);
	const G3Zone &z7 = b->gore->zones[GZ_CHEST_FRONT];
	CHECK(c->health <= 0, "G3.1 killed (%d)", c->health);
	CHECK(BoltOfEffect("gore/bloodcloud_s") == (int)z7.goreBolt, "G3.1 cloud on the gore bolt (%d, gore %d cap %d)",
	      BoltOfEffect("gore/bloodcloud_s"), (int)z7.goreBolt, (int)z7.bodyCapBolt);
	CHECK(b->gore->killZone == GZ_CHEST_FRONT, "G3.1 killing zone 7 (%d)", b->gore->killZone);
	vec3_t cp;
	G3G_BoltWorld(c, c->ghoulInst, z7.bodyCapBolt, cp);
	CHECK(VectorCompare(cp, b->gore->killBoltPos), "G3.1 pool follows the body-side cap bolt %s / %s", vtos(cp),
	      vtos(b->gore->killBoltPos));

	// G3.2: single-player player without a body: the killing hit plays the hit sounds; a dead player nothing
	Reset();
	test::cvars["sk_toughness"] = 4; // player damage factor 1 (SPEC-720 3.1)
	vec3_t a2 = {200, 0, 0};
	edict_t *pl = Player(2, a2, 180);
	pl->health = 10;
	vec3_t dir = {1, 0, 0}, pt = {200, 0, 0};
	Dmg_Damage(pl, &edicts[0], &edicts[0], dir, pt, po, 20, 0, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(Count("sound:impact/gore/impact") == 1 && Count("sound:impact/player/hit") == 1,
	      "G3.2 killing hit on a SP player plays the hit sounds");
	test::log.clear();
	Dmg_Damage(pl, &edicts[0], &edicts[0], dir, pt, po, 20, 0, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(Count("sound:impact/") == 0, "G3.2 dead player: no hit sound");
	if (Count("sound:impact/"))
		DumpLog();

	// G3.14: the fraction (value 12) reaches the armour method as a float; the bypassed share is lost
	Reset();
	test::cvars["sk_toughness"] = 4;
	pl = Player(2, a2, 180);
	invs[1].armor = 50;
	Dmg_Damage(pl, &edicts[0], &edicts[0], dir, pt, po, 40, 0, DMGF_PROJECTILE, MOD_PISTOL1, 0, 0.5f);
	CHECK(invs[1].lastFraction == 0.5f && invs[1].armor == 30 && pl->health == 100,
	      "G3.14 armour 50, D 40, f 0.5: fraction %f armour %d health %d", invs[1].lastFraction, invs[1].armor,
	      pl->health);

	// G3.11: a severed piece starts with angles (0, yaw in [-30, 30], 0), paused, with the corrected physics
	Reset();
	test::cvars["ai_goretest"] = 1;
	p = Player(1, po, 0);
	c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	ZoneOwnerPart(c, GZ_ARM_LOWER_LEFT, part);
	Front(3, s, e);
	Shoot(p, c, s, e, {{"", part, 195}}, 300, DMGF_PROJECTILE, MOD_PISTOL1);
	int pieces = 0;
	for (int i = 1; i < globals.num_edicts; i++)
		if (edicts[i].inuse && (edicts[i].g3flags & G3F_PIECE)) {
			edict_t *pc = &edicts[i];
			pieces++;
			CHECK(pc->s.angles[PITCH] == 0 && pc->s.angles[ROLL] == 0 && fabsf(pc->s.angles[YAW]) <= 30,
			      "G3.11 piece angles %s", vtos(pc->s.angles));
			CHECK(pc->friction == 0.5f && pc->gravity == 1.0f && pc->airfriction == 0.1f && pc->buoyancy == 0.7f &&
			          pc->elasticity == 0.5f,
			      "G3.11 piece physics");
			CHECK(static_cast<Inst *>(pc->ghoulInst)->paused, "G3.11 piece paused");
		}
	CHECK(pieces == 1, "G3.11 one piece (%d)", pieces);
	test::cvars["ai_goretest"] = 0;
	if (failures)
		DumpLog();
}

// SPEC-722 Savegames / SPEC-700 kind "bodies": write the table and data of a wounded soldier, a dog and a cow, destroy
// the bodies, read them back
static void T722_save(void)
{
	printf("SPEC-722 savegame: body table, images (AIBM/AIBR sizes) and AIGZ round trip\n");
	Reset();
	test::chunks.clear();
	test::chunkReadPos = 0;
	vec3_t co = {0, 0, 0}, do_ = {100, 0, 0};
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	edict_t *d = Character("rottweiler", BODY_ROTTWEILER, "m_x_rottweiler", 100, do_, 0);
	if (!c || !d) {
		printf("  skipped (no fixture)\n");
		return;
	}
	G3Body *b = Body_Of(c);
	b->SetArmour(1);
	b->gore->zones[GZ_ARM_UPPER_LEFT].damage = 150;
	b->gore->killZone = GZ_CHEST_FRONT;
	b->SetAnim(Anim_Human("std_sarm_rt_n_n"), true);
	b->SetExpression(EXPR_PAIN, 10, true);
	b->deathTime = 12.5f;
	int armourBefore = b->ArmourPercent();
	int handBefore = b->RightHandType();
	uint32_t counter = 7;
	Body_AssignIds(&counter);
	uint32_t idC = b->saveId, idD = Body_Of(d)->saveId;
	CHECK(idC == 7 && idD == 8 && counter == 9 && Body_MaxId() == 8, "ids %u %u counter %u", idC, idD, counter);
	Body_WriteTable();
	Body_WriteData();
	// table 1 + 2 AICC, then soldier image + AIGZ, dog image + AIGZ
	CHECK(test::chunks.size() == 7, "chunk count %d", (int)test::chunks.size());
	if (test::chunks.size() == 7) {
		CHECK(test::chunks[3].second.size() == 492 && test::chunks[4].second.size() == 104, "soldier: 492 + 26 ints");
		CHECK(test::chunks[5].second.size() == 144 && test::chunks[6].second.size() == 40, "dog: 144 + 10 ints");
		CHECK((uint32_t)test::chunks[3].first == ('A' << 24 | 'I' << 16 | 'B' << 8 | 'M'), "soldier id AIBM");
	}
	Body_Destroy(c);
	Body_Destroy(d);
	Body_ReadTable();
	Body_ReadData();
	G3Body *nb = Body_Of(c);
	CHECK(nb && nb != NULL && nb->saveId == idC && Body_ById(idC) == nb, "soldier body re-created with its id");
	if (nb) {
		CHECK(nb->cur == Anim_Human("std_sarm_rt_n_n"), "current record %s", nb->cur ? nb->cur->name : "-");
		CHECK(nb->expr == EXPR_PAIN && nb->exprLock && nb->deathTime == 12.5f, "expression and death time");
		CHECK(nb->gore->zones[GZ_ARM_UPPER_LEFT].level == 2, "zone level from AIGZ (%d)",
		      nb->gore->zones[GZ_ARM_UPPER_LEFT].level);
		CHECK(nb->gore->killZone == GZ_CHEST_FRONT && nb->gore->armourType == 1, "kill zone and armour type");
		CHECK(nb->ArmourPercent() == armourBefore && nb->RightHandType() == handBefore, "armour %d/%d hand %d/%d",
		      nb->ArmourPercent(), armourBefore, nb->RightHandType(), handBefore);
	}
	CHECK(Body_Of(d) && Body_Of(d)->family == BODY_ROTTWEILER && Body_Of(d)->saveId == idD, "dog re-created");
	CHECK(Count("readchunk:") == 0, "all chunks read in order");
	if (failures)
		DumpLog();
}

// SPEC-720 10.5 second estimator (crouched: no leg case) and the deathmatch player pain reaction (SPEC-720 §13)
static void T720_estimate2_pain(void)
{
	printf("SPEC-720 10.5 second estimator; deathmatch player pain reaction\n");
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0};
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 1000, co, 180);
	c->enemy = p;
	G3Body *b = Body_Of(c);
	vec3_t dir = {1, 0, 0}, low = {-8, 0, -20};
	G3_ResetHint();
	// standing: a low spread hit from the front lands on the legs
	Dmg_Damage(c, p, p, dir, low, po, 4, 0, DMGF_SPREAD, MOD_SHOTGUN);
	int z1 = b->gore->lastZone;
	CHECK(z1 == 16 || z1 == 18 || z1 == 20 || z1 == 21 || z1 == 0, "standing low hit: leg zone (%d)", z1);
	// crouched: the second estimator has no leg case (front set 7, 5, 3, 13)
	const G3Anim *crouch = nullptr;
	for (int i = 0; i < g3_numHumanAnims && !crouch; i++)
		if (g3_humanAnims[i].posture == POSTURE_CROUCH && b->HasAnim(&g3_humanAnims[i]))
			crouch = &g3_humanAnims[i];
	CHECK(crouch && b->SetAnim(crouch, true) && b->cur == crouch, "crouched record %s", crouch ? crouch->name : "-");
	G3_ResetHint(); // no spread hint left from the first hit
	Dmg_Damage(c, p, p, dir, low, po, 4, 0, DMGF_SPREAD, MOD_SHOTGUN);
	int z2 = b->gore->lastZone;
	CHECK(z2 == 7 || z2 == 5 || z2 == 3 || z2 == 13 || z2 == 16 || z2 == 0, "crouched front hit: second set (%d)", z2);
	// from his left (G3.21 verify): 7, 5, 18, 12, 6 (+0), never a leg zone below the thigh
	for (int i = 0; i < 20; i++) {
		G3_ResetHint();
		// the soldier faces -x (yaw 180): his left is -y, a shot from there travels along +y
		vec3_t lo = {0, -200, 0}, lp = {0, -8, -20}, ld = {0, 1, 0};
		b->SetAnim(crouch, false);
		Dmg_Damage(c, p, p, ld, lp, lo, 1, 0, DMGF_SPREAD, MOD_SHOTGUN);
		int z = b->gore->lastZone;
		CHECK(z == 7 || z == 5 || z == 18 || z == 12 || z == 6 || z == 0, "crouched, from the left: %d", z);
	}

	// deathmatch player with a body: a non-lethal arm hit plays a hit reaction for 0.2 s, then the previous record
	Reset();
	test::cvars["deathmatch"] = 1;
	vec3_t a2 = {0, 0, 0};
	edict_t *pl = Player(2, a2, 180);
	const Model *m = nullptr;
	for (auto &mm : test::lib)
		if (Lower(mm.name) == "irqsoldier1")
			m = &mm;
	if (!m) {
		printf("  skipped (no fixture)\n");
		return;
	}
	Object *o = new Object();
	o->m = *m;
	pl->ghoulInst = o->CreateInstance();
	pl->ghoulInst->SetUserValue(pl);
	VectorAdd(pl->s.origin, pl->mins, pl->absmin);
	VectorAdd(pl->s.origin, pl->maxs, pl->absmax);
	G3Body *pb = Body_Create(pl, BODY_MESO);
	pb->Precache();
	const G3Anim *idle = Anim_Human("std_ialert_n_p_n");
	pb->SetAnim(idle, true);
	edict_t *sh = Player(1, po, 0);
	char arm[64];
	ZoneOwnerPart(pl, GZ_ARM_UPPER_LEFT, arm);
	vec3_t s, e;
	Front(22, s, e);
	Shoot(sh, pl, s, e, {{"", arm, 195}}, 20, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(pl->health > 0 && pb->cur && pb->cur->cls == ANIMCLS_HIT, "pain record playing (%s, health %d)",
	      pb->cur ? pb->cur->name : "-", pl->health);
	level.time += 0.1f;
	pb->Think();
	CHECK(pb->cur && pb->cur->cls == ANIMCLS_HIT, "kept while not done (%s)", pb->cur ? pb->cur->name : "-");
	pb->finished = true; // its EOS
	pb->Think();
	CHECK(pb->cur == idle, "previous record back after the reaction's end (%s)", pb->cur ? pb->cur->name : "-");
	test::cvars["deathmatch"] = 0;
}

// SPEC-721 1.5: the character's own g_half_head record (head skin, dead face) and the cap bolt-on records
static void T721_halfhead(void)
{
	printf("SPEC-721 1.5 g_half_head record per head, cap bolt-on records\n");
	Reset();
	vec3_t co = {0, 0, 0};
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	G3Body *b = Body_Of(c);
	Inst *in = static_cast<Inst *>(c->ghoulInst);
	GhoulID hm = in->obj->FindMaterial("h"), fm = in->obj->FindMaterial("f");
	if (!hm || !fm) {
		printf("  skipped (fixture without h/f materials)\n");
		return;
	}
	GhoulID hs = in->obj->FindSkin(hm, in->obj->m.skins.empty() ? "" : "h_m_3_s", 0);
	GhoulID fs = in->obj->FindSkin(fm, "f_m_3_n", 0);
	if (!hs || !fs) {
		printf("  skipped (fixture without the skins)\n");
		return;
	}
	in->SetSkin(hm, hs, 0);
	in->SetSkin(fm, fs, 0);
	test::log.clear();
	b->gore->halfHeadObj = nullptr;
	b->gore->Setup();
	CHECK(Count("ggfind:Enemy/bolt/g_half_head/H_M_3_S/F_M_3_d") + Count("ggfind:Enemy/bolt/g_half_head/h_m_3_s/f_m_3_d") == 1,
	      "half-head record keyed by head and dead face");
	CHECK(Count("ggfind:Enemy/bolt/g_guts_lrg//all") >= 1, "g_guts_lrg record with all skins");
	if (failures)
		DumpLog();
}

static void T722_animals(void)
{
	printf("SPEC-720 B12 dog and cow\n");
	Reset();
	vec3_t co = {0, 0, 0};
	edict_t *d = Character("rottweiler", BODY_ROTTWEILER, "m_x_rottweiler", 100, co, 180);
	if (!d) {
		printf("  skipped (no fixture)\n");
		return;
	}
	vec3_t dir = {1, 0, 0}, pt = {0, 0, 0}, org = {-100, 0, 0};
	Dmg_Damage(d, &edicts[0], &edicts[0], dir, pt, org, 30, 0, DMGF_PROJECTILE, MOD_PISTOL1);
	CHECK(d->health == 100, "projectile without Ghoul hits does nothing (%d)", d->health);
	G3Body *b = Body_Of(d);
	CHECK(b->gore && b->gore->zones[0].scale == 10.0f && b->gore->zones[3].scale == 1.0f, "dog scales");
}

static void T720_dmflags(void)
{
	printf("SPEC-790 effective dmflags\n");
	Reset();
	test::cvars["deathmatch"] = 5;
	test::cvars["dmflags"] = 0x100 | 0x8;
	CHECK(Dmg_DMFlags() == 0x10, "Realistic: realistic on, friendly-fire block and no-falling off (%x)", Dmg_DMFlags());
	test::cvars["deathmatch"] = 4;
	test::cvars["dmflags"] = 0;
	CHECK(Dmg_DMFlags() == 0x80, "CTF: team play forced (%x)", Dmg_DMFlags());
	test::cvars["deathmatch"] = 0;
	test::cvars["dmflags"] = 0xffff;
	CHECK(Dmg_DMFlags() == 0, "single player: all off");
}

static void T720_safetynet(void)
{
	printf("G3 safety net: a traced hit without the trace filter fills the hit list itself\n");
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	char arm[64];
	ZoneOwnerPart(c, GZ_ARM_UPPER_RIGHT, arm);
	Front(22, s, e);
	nextShot = {{"", arm, 195}, {"", arm, 211}};
	vec3_t dir = {1, 0, 0}, pt = {-5, 0, 22};
	Dmg_Damage(c, p, p, dir, pt, s, 40, 40, DMGF_PROJECTILE, MOD_ASSAULTRIFLE);
	nextShot.clear();
	CHECK(c->health == 80, "arm hit through the safety net: health %d", c->health);
}

static void T720_helper(void)
{
	printf("Shot filter: a hit on a helper volume resolves to its owner (SPEC-780 15 / SPEC-700 16)\n");
	Reset();
	vec3_t po = {-200, 0, 0}, co = {0, 0, 0}, s, e;
	edict_t *p = Player(1, po, 0);
	edict_t *c = Character("irqsoldier1", BODY_MESO, "m_irq_msoldier1", 100, co, 180);
	c->enemy = p;
	edict_t *h = G_Spawn();
	h->svflags = 0x82;
	h->owner = c;
	char chest[64];
	ZoneOwnerPart(c, GZ_CHEST_FRONT, chest);
	Front(15, s, e);
	nextShot = {{"", chest, 190}, {"", chest, 206}};
	test::traceTarget = h;
	trace_t tr = Dmg_ShotTrace(s, nullptr, nullptr, e, p, MASK_SHOT);
	CHECK(tr.ent == c, "trace resolved to the character (%d)", tr.ent ? G_EdictNum(tr.ent) : -1);
	vec3_t dir = {1, 0, 0};
	Dmg_Damage(tr.ent, p, p, dir, tr.endpos, s, 40, 40, DMGF_PROJECTILE, MOD_PISTOL1);
	nextShot.clear();
	test::traceTarget = nullptr;
	CHECK(c->health == 60, "character hurt through its helper: health %d", c->health);
}

int main(int argc, char **argv)
{
	// optional argument: the fixture directory (default: the Windows path above)
	std::string fixdir = fixtures;
	if (argc > 1) {
		fixdir = argv[1];
		if (!fixdir.empty() && fixdir.back() != '/' && fixdir.back() != '\\')
			fixdir += '/';
	}
	test::InstallEngine();
	std::vector<std::string> files = {"meso_irqsoldier1.txt", "ecto_commander.txt", "rottweiler_rottweiler.txt",
		"bolt_all.txt"};
	for (auto &f : files) {
		std::string path = fixdir + f;
		if (!LoadModels(path.c_str(), test::lib)) {
			printf("fixture %s missing - run gen_mockmodels.py (needs the game data); skipping\n", path.c_str());
			return 0;
		}
	}
	T720_1();
	T720_2();
	T720_3();
	T720_4();
	T720_5();
	T720_6();
	T720_7();
	T720_8();
	T720_dmflags();
	T720_safetynet();
	T720_helper();
	T721_1();
	T721_2();
	T721_3();
	T721_4();
	T721_5();
	T721_6();
	T721_7();
	T721_10();
	T722_deaths();
	T722_armour();
	T722_expression();
	T722_rootyaw();
	T_A005();
	T722_save();
	T720_estimate2_pain();
	T721_halfhead();
	T722_animals();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
