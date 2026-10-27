/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - the weapon state machine: think, attack starts, shots, ammunition per shot, end of
 * sequence, reloading, requests, switching, dropping, holstering and the player-state writes.
 * Implements: SPEC-600 (Behaviour 3-9, 11), SPEC-601 (Behaviour 5, 6), SPEC-602 (Behaviour 2-4, server side)
 */
#include <stdio.h>
#include <stdlib.h>
#include "pm_local.h"

namespace om {

/* buttons as seen by the current think (after the cinematic-freeze rule) */
static int s_att, s_alt;

static float fire_time(Inventory *inv, bool alt)
{
    if (!inv->PredictingOwner())
        return 0.f;
    edict_t *o = inv->Owner();
    if (alt)
        return g_sv.GetFireTimeB ? g_sv.GetFireTimeB(o) : 0.f;
    return g_sv.GetFireTimeA ? g_sv.GetFireTimeA(o) : 0.f;
}

static void clear_fire_time(Inventory *inv, bool alt)
{
    if (!inv->PredictingOwner())
        return;
    edict_t *o = inv->Owner();
    if (alt) {
        if (g_sv.ClearFireTimeB)
            g_sv.ClearFireTimeB(o);
    } else if (g_sv.ClearFireTimeA)
        g_sv.ClearFireTimeA(o);
}

/* SPEC-AMEND-006 I7.3: at the start of every server reload attempt and every draw both pending fire times are
   cleared, whether the owner predicts or not (without prediction they are normally 0) */
static void clear_both_fire_times(Inventory *inv)
{
    if (IsClient() || !g_haveSv)
        return;
    edict_t *o = inv->Owner();
    if (g_sv.ClearFireTimeA)
        g_sv.ClearFireTimeA(o);
    if (g_sv.ClearFireTimeB)
        g_sv.ClearFireTimeB(o);
}

/* ---------------------------------------------------------------------------------------------- */
/* player state                                                                                   */
/* ---------------------------------------------------------------------------------------------- */
void Inventory::WriteCounters()
{
    if (IsClient())
        return;
    unsigned char *ps = PS();
    if (!ps)
        return;
    *(short *)(ps + PS_CLIP) = (short)CurrentClip();
    *(short *)(ps + PS_RESERVE) = (short)CurrentReserve();
}

void Inventory::WritePSWeapon()
{
    if (IsClient())
        return;
    unsigned char *ps = PS();
    PmTrace("WritePSWeapon inv %p ctx %p owner %p ps %p inst %d type %d", this, st.ctx, Owner(), ps,
            PmInstId(st.inst), st.animWeapon);
    if (!ps)
        return;
    *(IGhoulInst **)(ps + PS_VIEWINST) = st.inst;
    *(unsigned short *)(ps + PS_VIEWTYPE) = (unsigned short)st.animWeapon;
}

/* ---------------------------------------------------------------------------------------------- */
/* think (SPEC-600 Behaviour 3)                                                                   */
/* ---------------------------------------------------------------------------------------------- */
void Inventory::Think()
{
    OwnerContext *c = st.ctx;
    if (!c)
        return;
    const bool client = IsClient();
    const float now = Now();
    if (client) {
        g_firedPrimary = 0;
        g_firedAlt = 0;
        if (c->reloadOneShot) {
            c->reloadOneShot = 0;
            PredictedReloadStart();
        }
        ClientCountersFromPS();
        if (!st.holstered)
            SwitchFollower();
    }
    /* 1. switch completion (on the client the follower completes it from the server's instance) */
    if (st.switchNow && !st.holstered && !client)
        CompleteSwitch();
    /* 2. holster handling, 3. cinematic freeze */
    int att = c->attack, alt = c->altAttack, x1 = c->extra1, x2 = c->extra2;
    if (c->cinematicFreeze)
        att = alt = x1 = x2 = 0;
    HolsterThink(att, alt);
    s_att = att;
    s_alt = alt;
    /* 4. the weapon's own rule */
    PerThinkRule(att, alt, x1, x2);
    s_att = att;
    s_alt = alt;
    /* 5. requests */
    if (c->cinematicFreeze)
        return;
    bool requestDone = false;
    if (st.pendingRequest && (st.ready != 0 || st.holstered || (client && st.dictated)))
        requestDone = ProcessRequest();
    /* 6. attacks (not in a think whose request did something; the items think still runs then, as observed) */
    if (!st.inst)
        return;
    if (!st.holstered && !requestDone) {
        if (!StartAttack(false, att != 0))
            StartAttack(true, alt != 0);
    }
    /* 7. note and end-of-sequence events */
    if (st.inst)
        st.inst->ProcessEvents(now + 0.001f);
    /* 8. counters into the player state */
    if (!client && !st.predWeapon)
        WriteCounters();
    /* 9. items */
    ItemsThink();
}

/* ---------------------------------------------------------------------------------------------- */
/* attacks and shots                                                                              */
/* ---------------------------------------------------------------------------------------------- */
static int alt_cost(int w)
{
    switch (w) {
    case W_MACHINEGUN:
    case W_MPG: return 10;
    case W_SLUGGER: return 2;
    case W_FLAMEGUN: return 6;
    default: return 1;
    }
}

bool Inventory::CanPay(bool alt)
{
    int w = W();
    if (!Ammo())
        return true;
    if (w == W_KNIFE)
        return !alt || st.ammo[AM_KNIFE] >= 2;
    if (w == W_SNIPER && alt)
        return true;
    return CurSlot().clip >= (alt ? alt_cost(w) : 1);
}

/* SPEC-600 Behaviour 6: takes one shot's ammunition; when it cannot be paid and react is set, the automatic
   reload or the dry-fire starts instead. */
bool Inventory::TakeShot(bool alt, bool react)
{
    int w = W();
    if (!Ammo())
        return true;
    if (w == W_KNIFE) {
        if (!alt)
            return true;
        if (st.ammo[AM_KNIFE] >= 2) {
            st.ammo[AM_KNIFE]--;
            return true;
        }
        return false;
    }
    if (w == W_SNIPER && alt)
        return true;
    if (w <= 0 || w >= W_HURTHAND)
        return true;
    WeaponSlot &s = CurSlot();
    int cost = alt ? alt_cost(w) : 1;
    if (s.clip >= cost) {
        s.clip -= cost;
        return true;
    }
    if (!react)
        return false;
    if (w == W_FLAMEGUN) {
        FlameSound(false);
        if (!alt) {
            if (st.varD)
                Command("weaponbestsafe", 1);
            else
                DryFire();
        } else {
            if (st.varD && s.clip == 0 && !IsClient())
                Command("weaponbestsafe", 1);
            DryFire();
        }
        return false;
    }
    if (st.varD)
        StartReload(true);
    else
        DryFire();
    return false;
}

bool Inventory::StartAttack(bool alt, bool held)
{
    int w = W();
    if (!st.ready || w <= 0)
        return false;
    /* a predicting owner's pending fire time is asked only by a ready weapon, for both modes even without an
       alternate attack (observed: no fire-time queries while a sequence plays) */
    const float fireTime = fire_time(this, alt);
    if (alt && !kWeapons[w].hasAlt)
        return false;
    bool want = PredictingOwner() ? fireTime != 0.f : held;
    if (!want)
        return false;
    if (IsClient()) {
        if (alt)
            g_firedAlt = 1;
        else
            g_firedPrimary = 1;
    }
    if (w == W_SNIPER && alt) {
        /* the scope: no ammunition, its own rule (SPEC-600 Behaviour 10) */
        clear_fire_time(this, true);
        if (!st.scoped) {
            if (!Play("scopein"))
                return false;
            st.ready = 0;
            st.wstate = WS_FIRE2;
            return true;
        }
        if (!st.zoomHeld && Play("scopeout")) {
            st.ready = 0;
            st.wstate = WS_FIRE2;
            SetFov(95.f);
            ScopeModel(false);
            st.savedZoom = 0;
            st.scoped = 0;
            return true;
        }
        return false;
    }
    const char *seq = alt ? AltSeq() : PrimarySeq();
    if (w == W_KNIFE && alt)
        st.knifeThrowOk = CanPay(true) ? 1 : 0;
    if (!TakeShot(alt, true)) {
        clear_fire_time(this, alt);
        return false;
    }
    float now = Now();
    float start = now;
    if (PredictingOwner()) {
        bool useNow = alt ? (w == W_ROCKET) : (w == W_ASSAULT || w == W_MPISTOL);
        if (!useNow)
            start = fireTime;
        clear_fire_time(this, alt);
    }
    st.ready = 0;
    st.wstate = alt ? WS_FIRE2 : WS_FIRE1;
    /* the first shot of a fire sequence is pre-paid for the assault rifle, machinegun and rocket alternate. The
       machine pistol's start leaves the flag alone: its first shot takes a second round unless the flag is still
       set (observed on the original: a new inventory's flag 1 makes the first burst's first shot free, later
       presses pay twice; SPEC-600 Behaviour 10 says always twice) */
    if (alt ? (w == W_ROCKET) : (w == W_ASSAULT || w == W_MACHINEGUN))
        st.prepaid = 1;
    if (w == W_PISTOL1 || w == W_PISTOL2)
        st.subPose = 0;
    if (w == W_FLAMEGUN && !alt)
        FlameSound(true);
    Play(seq, 1, start);
    return true;
}

void Inventory::DryFire()
{
    if (Play(DrySeq())) {
        st.ready = 0;
        st.wstate = WS_IDLE;
        if (W() == W_PISTOL1 || W() == W_PISTOL2)
            st.subPose = 0;
    }
}

/* fire / altfire notes (SPEC-600 Behaviour 5) */
void Inventory::FireNote(bool alt, const char *arg)
{
    if (st.wstate != (alt ? WS_FIRE2 : WS_FIRE1))
        return;
    int w = W();
    if (w <= 0)
        return;
    if (!ShotGate(alt))
        return;
    if (!alt && arg && arg[0] == '1' && arg[1] == 0) {
        /* flamethrower fuel note */
        if (!TakeShot(false, false)) {
            FlameSound(false);
            st.spread = 0;
            GoIdle();
            return;
        }
    }
    if (IsClient()) {
        if (alt) {
            if (g_haveCl && g_cl.FireAltFx)
                g_cl.FireAltFx(st.ctx, Owner(), this);
        } else if (g_haveCl && g_cl.FirePrimaryFx)
            g_cl.FirePrimaryFx(st.ctx, Owner(), this);
    } else if (g_haveSv) {
        if (alt) {
            if (g_sv.FireAlt)
                g_sv.FireAlt(st.ctx, Owner(), this);
        } else if (g_sv.FirePrimary)
            g_sv.FirePrimary(st.ctx, Owner(), this);
    }
    if (st.ctx && st.ctx->kick)
        st.ctx->kick[0] = -(alt ? kWeapons[w].kick2 : kWeapons[w].kick1);
}

/* ---------------------------------------------------------------------------------------------- */
/* end of a sequence (SPEC-600 Behaviour 7)                                                       */
/* ---------------------------------------------------------------------------------------------- */
/* refire condition at a fire sequence's end (SPEC-600 Behaviour 7, SPEC-602 Behaviour 2) */
bool Inventory::WantsRefire(bool alt)
{
    if (st.pendingRequest)
        return false;
    int w = W();
    bool useButton = !PredictingOwner() || (!alt && (w == W_KNIFE || w == W_MPG || w == W_FLAMEGUN));
    if (useButton)
        return (alt ? s_alt : s_att) != 0;
    return fire_time(this, alt) != 0.f;
}

void Inventory::GoIdle()
{
    st.ready = 1;
    st.wstate = WS_IDLE;
    ChooseIdle();
}

void Inventory::EndOfSequence()
{
    /* SPEC-600 Behaviour 10, flamethrower: owners with server flag 0x200 (believed: bots) fire one extra attack
       directly whenever an in-view sequence ends while the attack button is held */
    if (!IsClient() && W() == W_FLAMEGUN && s_att && Owner() && g_haveSv &&
        (*(int *)((char *)Owner() + EDICT_SVFLAGS) & 0x200)) {
        if (s_alt) {
            if (g_sv.FireAlt)
                g_sv.FireAlt(st.ctx, Owner(), this);
        } else if (g_sv.FirePrimary)
            g_sv.FirePrimary(st.ctx, Owner(), this);
    }
    if (EndRule(st.seqName))
        return;
    int w = W();
    if ((st.wstate == WS_FIRE1 || st.wstate == WS_FIRE2) && Ammo() && w > 0 && w != W_KNIFE && w != W_FLAMEGUN &&
        w < W_HURTHAND && st.varD && CurSlot().clip == 0) {
        StartReload(true);
        return;
    }
    switch (st.wstate) {
    case WS_IDLE:
    case WS_RELOAD: GoIdle(); break;
    case WS_HAND: HandFinished(); break;
    case WS_FIRE1:
    case WS_FIRE2: {
        bool alt = st.wstate == WS_FIRE2;
        bool useButton = !PredictingOwner() || (!alt && (w == W_KNIFE || w == W_MPG || w == W_FLAMEGUN));
        float t = 0.f;
        bool want;
        if (st.pendingRequest)
            want = false;
        else if (useButton)
            want = (alt ? s_alt : s_att) != 0;
        else
            want = (t = fire_time(this, alt)) != 0.f;
        if (want) {
            st.spread++;
            if (!TakeShot(alt, true)) {
                /* the automatic reload (if any) has moved the rounds; going idle replaces its animation */
                GoIdle();
                break;
            }
            float start = Now();
            if (!useButton) {
                bool useNow = alt ? (w == W_ROCKET)
                                  : (w == W_ASSAULT || w == W_MPISTOL);
                if (!useNow)
                    start = t;
            }
            /* a predicting owner's pending fire time of this mode is dropped at every refire, also where the button
               decides (knife, MPG, flamethrower primaries): the client's own refire times must not start a new
               attack after the release (observed on the original server) */
            clear_fire_time(this, alt);
            if (IsClient()) {
                if (alt)
                    g_firedAlt = 1;
                else
                    g_firedPrimary = 1;
            }
            if (alt ? (w == W_ROCKET) : (w == W_ASSAULT || w == W_MACHINEGUN || w == W_MPISTOL))
                st.prepaid = 1;
            Play(RefireSeq(alt), 1, start);
        } else {
            st.spread = 0;
            if (w == W_FLAMEGUN && !alt)
                FlameSound(false);
            GoIdle();
        }
        break;
    }
    case WS_PUTAWAY: st.switchNow = 1; break;
    }
}

/* ---------------------------------------------------------------------------------------------- */
/* reloading (SPEC-600 Behaviour 11)                                                              */
/* ---------------------------------------------------------------------------------------------- */
bool Inventory::StartReload(bool automatic)
{
    (void)automatic;
    int w = W();
    if (IsClient())
        return false; /* the predictor never starts a reload on its own (SPEC-602 Behaviour 1) */
    clear_both_fire_times(this);
    if (w <= 0 || w >= W_HURTHAND || w == W_KNIFE || w == W_FLAMEGUN)
        return false;
    const char *seq = ReloadSeq();
    if (!seq)
        return false;
    WeaponSlot &s = CurSlot();
    int size = kWeapons[w].clip;
    int type = kWeapons[w].ammo1;
    if (s.clip >= size && !st.varE)
        return false;
    st.spread = 0;
    int loaded = 0;
    bool shellByShell = (w == W_SHOTGUN && st.varC && !st.varE);
    if (!Ammo()) {
        loaded = 1;
    } else if (shellByShell) {
        loaded = (s.clip < size && st.ammo[type] > 0) ? 1 : 0;
    } else if (st.varE) {
        if (st.ammo[type] >= size) {
            st.ammo[type] -= size;
            s.clip = size;
            loaded = size;
        } else if (st.ammo[type] > 0) {
            s.clip = st.ammo[type];
            loaded = st.ammo[type];
            st.ammo[type] = 0;
        }
    } else {
        int need = size - s.clip;
        if (need > st.ammo[type])
            need = st.ammo[type];
        if (need > 0) {
            s.clip += need;
            st.ammo[type] -= need;
            loaded = need;
        }
    }
    unsigned char *ps = PS();
    if (!loaded) {
        if (!HasSeq(DrySeq()))
            GoIdle();
        else
            DryFire();
        if (s.clip == 0) {
            int best = BestSlot(true);
            Command("weaponbestsafe", 1);
            if (best >= 0 && best != st.curSlot)
                return false;
        }
        if (ps)
            ps[PS_WEAPONSEQ]++;
        return false;
    }
    if (w == W_SNIPER) {
        st.stanceB = 1;
        st.transitionLast = 1;
    }
    if (st.varC) {
        if (g_haveSv && g_sv.ReloadStarted)
            g_sv.ReloadStarted(Owner());
        if (w == W_SNIPER && st.scoped) {
            SetFov(95.f);
            ScopeModel(false);
            st.scoped = 0;
            st.zoomHeld = 0;
        }
        st.wstate = WS_RELOAD;
        st.ready = 0;
        Play(seq);
        if (w == W_PISTOL2)
            st.subPose = 0;
    }
    if (ps)
        ps[PS_WEAPONSEQ]++;
    WriteCounters();
    return true;
}

/* ---------------------------------------------------------------------------------------------- */
/* requests, selection and switching (SPEC-600 Behaviour 8, 9; SPEC-601 Behaviour 5)              */
/* ---------------------------------------------------------------------------------------------- */
bool Inventory::Usable(int slot, bool allowEmpty)
{
    if (slot < 0 || slot >= NUM_SELECTABLE)
        return false;
    const WeaponSlot &s = st.slots[slot];
    if (!s.weapon)
        return false;
    if (IsClient() || allowEmpty)
        return true;
    if (s.clip > 0 || !Ammo() || s.weapon == W_KNIFE)
        return true;
    int type = kWeapons[s.weapon].ammo1;
    if (type == AM_FUEL)
        return false;
    return type > 0 && st.ammo[type] > 0;
}

int Inventory::BestSlot(bool safe)
{
    const int *list = safe ? kBestSafe : kBestUnsafe;
    for (int i = 0; i < 12; i++) {
        int slot = SlotOfWeapon(list[i]);
        if (slot >= 0 && st.slots[slot].weapon == list[i] && Usable(slot, false))
            return slot;
    }
    return -1;
}

int Inventory::BestWeapon()
{
    if (!strcmp(st.tag, "safe")) {
        for (int i = 0; i < 3; i++)
            if (st.curWeapon == kKeepExplosive[i])
                return st.curWeapon;
        int s = BestSlot(true);
        return s >= 0 ? st.slots[s].weapon : 0;
    }
    int s = BestSlot(false);
    return s >= 0 ? st.slots[s].weapon : 0;
}

static bool any_weapon_owned(const InvState &st)
{
    for (int i = 0; i < NUM_SELECTABLE; i++)
        if (st.slots[i].weapon >= 1 && st.slots[i].weapon <= 12)
            return true;
    return false;
}

bool Inventory::ProcessRequest()
{
    int code = st.pendingRequest;
    bool dictated = st.dictated != 0;
    st.pendingRequest = 0;
    st.dictated = 0;
    if (IsClient() && !dictated) {
        ChooseIdle();
        return false;
    }
    if (!IsClient() && !any_weapon_owned(st) && code != REQ_RELOAD)
        return false;
    int slot = -1;
    switch (code) {
    case REQ_RELOAD:
        if (!st.varC)
            return false;
        return StartReload(false);
    case REQ_NEXT:
    case REQ_PREV: {
        int start = st.lastSlot;
        if (start < 0 || start >= NUM_SELECTABLE)
            start = 0;
        int i = start;
        for (;;) {
            i = (code == REQ_NEXT) ? (i + 1) % NUM_SELECTABLE : (i + NUM_SELECTABLE - 1) % NUM_SELECTABLE;
            if (i == start)
                break;
            if (Usable(i, st.varG != 0)) {
                slot = i;
                break;
            }
        }
        break;
    }
    case REQ_BESTSAFE:
    case REQ_BESTUNSAFE: slot = BestSlot(code == REQ_BESTSAFE); break;
    case REQ_LOSE:
    case REQ_DROP: DropOrLose(code == REQ_LOSE); return true;
    default:
        slot = code - REQ_SLOT0;
        if (slot >= NUM_WSLOTS || !st.slots[slot].weapon)
            return false;
        if (IsClient() && slot >= HAND_HURT_SLOT) {
            DropOrLose(slot == HAND_HURT_SLOT);
            return true;
        }
        break;
    }
    if (slot < 0)
        return false;
    if (st.holstered && !s_att && !s_alt) {
        st.targetSlot = slot;
        return false;
    }
    return SwitchToSlot(slot, dictated);
}

void Inventory::PlayPutaway()
{
    const char *seq = PutawaySeq();
    if (seq && Play(seq)) {
        st.ready = 0;
        st.wstate = WS_PUTAWAY;
    } else
        st.switchNow = 1;
}

bool Inventory::SwitchToSlot(int slot, bool dictated)
{
    (void)dictated;
    if (slot < 0 || slot >= NUM_WSLOTS)
        return false;
    if (slot == st.curSlot && st.curWeapon)
        return false;
    if (IsClient()) {
        /* SPEC-602 Behaviour 4: the predictor plays its own put-away; without one (or without a weapon) the
           follower completes at once from the server's instance (observed: adopted in the same think) */
        st.targetSlot = slot;
        if (st.curWeapon && st.inst) {
            DeselectRule();
            const char *pseq = PutawaySeq();
            if (pseq && Play(pseq)) {
                st.ready = 0;
                st.wstate = WS_PUTAWAY;
                return true;
            }
        }
        st.switchNow = 1;
        SwitchFollower();
        return true;
    }
    st.targetSlot = slot;
    if (!st.curWeapon || !st.inst) {
        SelectSlot(slot, true);
        return true;
    }
    DeselectRule();
    const char *seq = PutawaySeq();
    if (!seq || !HasSeq(seq)) {
        SelectSlot(slot, true);
        return true;
    }
    if (PredictingOwner())
        PrepareNextInstance(slot);
    Play(seq);
    st.ready = 0;
    st.wstate = WS_PUTAWAY;
    return true;
}

/* SPEC-602 Behaviour 4 with SPEC-AMEND-006 I7.4, server with a predicting owner, steps 2-5 of a predicted switch
   (also used when the holster is turned on): remember the target, make the "next" instance from the target's model
   and put it into the player state, one idle choice for the target on it, then the target's weapon id, clip and
   reserve into the player state. The caller then starts the put-away on the old instance, so the sequence-name
   word ends up holding the put-away's name. */
void Inventory::PrepareNextInstance(int slot)
{
    if (slot < 0 || slot >= NUM_WSLOTS)
        return;
    int nw = st.slots[slot].weapon;
    if (nw <= 0 || nw >= NUM_WEAPONS)
        return;
    st.predWeapon = nw;
    st.predAnim = nw;
    st.predSlot = slot;
    NewViewInstance(nw, true);
    unsigned char *ps = PS();
    if (ps)
        *(IGhoulInst **)(ps + PS_VIEWINST) = st.nextInst;
    if (st.nextInst) {
        IGhoulInst *cur = st.inst;
        int keepAnim = st.animWeapon;
        st.inst = st.nextInst;
        st.animWeapon = nw;
        ChooseIdle();
        st.animWeapon = keepAnim;
        st.inst = cur;
    }
    if (ps) {
        *(unsigned short *)(ps + PS_VIEWTYPE) = (unsigned short)nw;
        *(short *)(ps + PS_CLIP) = (short)st.slots[slot].clip;
        int type = kWeapons[nw].ammo1;
        *(short *)(ps + PS_RESERVE) = (short)((type > 0 && type < NUM_AMMO) ? st.ammo[type] : 0);
    }
}

/* SPEC-600 Behaviour 8: selection of a slot (new instance, player state, visible rounds, idle, draw) */
void Inventory::SelectSlot(int slot, bool draw)
{
    PmTrace("SelectSlot inv %p slot %d weapon %d draw %d (was %d, inst %d) now %.3f", this, slot,
            st.slots[slot].weapon, (int)draw, st.curWeapon, PmInstId(st.inst), Now());
    st.curSlot = slot;
    st.curWeapon = st.slots[slot].weapon;
    st.animWeapon = st.curWeapon;
    st.lastSlot = slot;
    if (st.curWeapon == W_KNIFE)
        st.knifeBloody = 0;
    NewViewInstance(st.curWeapon, false);
    WritePSWeapon();
    WriteCounters();
    VisibleRounds(true, false);
    if (!st.holstered)
        ChooseIdle();
    if (draw)
        StartDraw();
}

/* The animated weapon's draw sequence with the stance it sets (SPEC-600 Behaviour 10: pistol1 `ready_a` = stance a,
   pistol2 = stance a, sniper = "drawn"). Used by every path that draws: a plain selection, the end of a predicted
   switch on the server and the client's switch follower (observed on the original: the same words change). */
bool Inventory::StartDraw()
{
    /* SPEC-AMEND-006 I7.3: server - both pending fire times; client - both "fired this frame" exports and the
       synchronised flag */
    if (IsClient()) {
        g_firedPrimary = 0;
        g_firedAlt = 0;
    } else
        clear_both_fire_times(this);
    st.synced = 0;
    const char *seq = DrawSeq();
    if (!seq || !Play(seq))
        return false;
    st.ready = 0;
    st.wstate = (st.animWeapon >= W_HURTHAND) ? WS_HAND : WS_IDLE;
    switch (st.animWeapon) {
    case W_PISTOL1:
        st.p1Stance = 1;
        st.subPose = 0;
        break;
    case W_PISTOL2:
        st.p2Stance = 0;
        st.subPose = 0;
        break;
    case W_SNIPER:
        st.stanceB = 1;
        st.transitionLast = 1;
        st.savedZoom = 0;
        break;
    default: break;
    }
    return true;
}

void Inventory::CompleteSwitch()
{
    st.switchNow = 0;
    if (st.predWeapon && st.nextInst && x && x->nextOwned && !IsClient()) {
        /* SPEC-AMEND-006 I7.4: the next instance becomes current; only the remembered weapon id is cleared
           (0x020/0x024 and the next-instance word keep their values); no visible-rounds rule, no idle choice */
        DestroyInstance(st.inst);
        st.inst = st.nextInst;
        x->nextOwned = false;
        st.curSlot = st.predSlot;
        st.curWeapon = st.predWeapon;
        st.animWeapon = st.predAnim;
        st.lastSlot = st.curSlot;
        st.predWeapon = 0;
        WritePSWeapon();
        WriteCounters();
        StartDraw();
        return;
    }
    int slot = st.targetSlot;
    if (slot < 0 || slot >= NUM_WSLOTS || !st.slots[slot].weapon)
        return;
    SelectSlot(slot, true);
}

/* SPEC-003 slots 6-11 */
static void select_now(Inventory *inv, int slot)
{
    if (slot < 0 || slot >= NUM_WSLOTS || !inv->st.slots[slot].weapon)
        return;
    inv->st.switchNow = 0;
    inv->st.ready = 1;
    inv->st.wstate = WS_IDLE;
    inv->SelectSlot(slot, false);
}

int Inventory::SelectWeaponNow(int weapon)
{
    int slot = FindSlotOfWeapon(weapon);
    if (slot < 0)
        return 0;
    select_now(this, slot);
    return 1;
}

void Inventory::SelectSlotNow(int slot) { select_now(this, slot); }

void Inventory::SelectBestNow()
{
    int slot = BestSlot(!strcmp(st.tag, "safe"));
    if (slot >= 0)
        SelectSlotNow(slot); /* through the method table, as the original does */
}

void Inventory::RequestWeapon(int weapon)
{
    int slot = FindSlotOfWeapon(weapon);
    if (slot >= 0)
        st.pendingRequest = REQ_SLOT0 + slot;
}

void Inventory::RequestSlot(int slot) { st.pendingRequest = REQ_SLOT0 + slot; }

/* slot 11: the best weapon is resolved into a slot request at once (observed: request code 8 + slot) */
void Inventory::RequestBest()
{
    /* with no usable weapon the slot is -1 and the code becomes 7, the drop code (SPEC-AMEND-006 I7.14) */
    int slot = BestSlot(!strcmp(st.tag, "safe"));
    st.pendingRequest = REQ_SLOT0 + slot;
}

/* ---------------------------------------------------------------------------------------------- */
/* dropping and the hand pseudo-weapons                                                           */
/* ---------------------------------------------------------------------------------------------- */
void Inventory::DropOrLose(bool lose)
{
    int w = st.curWeapon;
    if (!IsClient()) {
        if (!w && st.pendingRequest == 0 && st.animWeapon >= W_HURTHAND) {
            /* the drop code left by a best-weapon request that found nothing usable (SPEC-AMEND-006 I7.14):
               the current weapon is cleared and no view model is left */
            DestroyInstance(st.inst);
            st.animWeapon = 0;
            WritePSWeapon();
            return;
        }
        if (!w || (st.varI & (1 << w)) || !st.varB)
            return;
    }
    DeselectRule();
    if (w && !IsClient()) {
        if (g_haveSv && g_sv.DropWeapon)
            g_sv.DropWeapon(Owner(), w, CurSlot().clip);
        CurSlot().clip = 0;
        CurSlot().weapon = 0;
        st.owned &= ~(1u << w);
    }
    int hand = lose ? HAND_HURT_SLOT : HAND_THROW_SLOT;
    st.targetSlot = hand;
    st.curSlot = hand;
    st.lastSlot = hand;
    st.animWeapon = lose ? W_HURTHAND : W_THROWHAND;
    st.curWeapon = 0;
    NewViewInstance(st.animWeapon, false);
    WritePSWeapon();
    WriteCounters();
    st.ready = 0;
    st.wstate = WS_HAND;
    Play(lose ? "hurthand" : "throwhand");
}

/* end of the hand pseudo-weapon: request the next weapon (empty tag) or the best one; commands are swallowed
   (ready -1) until the request is processed, which happens although the weapon is not "ready" (observed) */
void Inventory::HandFinished()
{
    /* SPEC-AMEND-006 I7.14: only the exact hand sequence ends this way, otherwise an ordinary idle choice */
    if (strcmp(st.seqName, "hurthand") != 0 && strcmp(st.seqName, "throwhand") != 0) {
        GoIdle();
        return;
    }
    if (!IsClient()) {
        if (st.tag[0] == 0)
            Command("weapnext", 1);
        else
            RequestBest();
    }
    ChooseIdle(); /* the hand has no idle: nothing is played */
    st.ready = -1;
}

int Inventory::RequestDrop()
{
    if (IsClient())
        return false;
    st.pendingRequest = REQ_DROP;
    return true;
}

/* slot 2: death drop */
void Inventory::DeathDrop()
{
    if (IsClient())
        return;
    ItemsSwitchOff();
    DeselectRule();
    int slot = st.dropSlot >= 0 ? st.dropSlot : st.curSlot;
    if ((st.varB || st.pendingRequest == REQ_DROP || st.dropSlot >= 0) && slot >= 0 && slot < NUM_SELECTABLE) {
        int w = st.slots[slot].weapon;
        if (w && !(st.varI & (1 << w))) {
            if (g_haveSv && g_sv.DropWeapon)
                g_sv.DropWeapon(Owner(), w, st.slots[slot].clip);
            st.slots[slot].weapon = 0;
            st.slots[slot].clip = 0;
            st.owned &= ~(1u << w);
        }
    }
    if (st.varF)
        DropAllWeapons();
}

/* slot 62 */
void Inventory::DropAllWeapons()
{
    if (IsClient())
        return;
    for (int i = 0; i < NUM_SELECTABLE; i++) {
        int w = st.slots[i].weapon;
        if (!w)
            continue;
        if (!(st.varI & (1 << w)) && g_haveSv && g_sv.DropWeapon)
            g_sv.DropWeapon(Owner(), w, st.slots[i].clip);
        st.slots[i].weapon = 0;
        st.slots[i].clip = 0;
        st.owned &= ~(1u << w);
    }
}

/* slot 58 */
void Inventory::Deselect() { DeselectRule(); }

/* ---------------------------------------------------------------------------------------------- */
/* holstering (SPEC-600 Behaviour 9)                                                              */
/* ---------------------------------------------------------------------------------------------- */
int Inventory::IsHolstered() { return st.holstered; }

void Inventory::ToggleHolster()
{
    st.holstered = !st.holstered;
    if (st.holstered && st.curWeapon) {
        DeselectRule();
        st.targetSlot = st.curSlot;
        /* SPEC-AMEND-006 I7.4: for a predicting owner the weapon comes back on a fresh instance prepared now */
        if (PredictingOwner() && PutawaySeq() && HasSeq(PutawaySeq()))
            PrepareNextInstance(st.targetSlot);
        PlayPutaway();
    }
    if (!IsClient())
        QueueEvent(5, 0);
}

void Inventory::HolsterThink(int &att, int &alt)
{
    if (!st.holstered)
        return;
    if (att || alt) {
        /* pressing an attack button unholsters (on both sides by the same rule, not replicated); the put-away
           has set switch-now, so the draw follows on the next think */
        st.holstered = 0;
        att = alt = 0;
    }
}

} /* namespace om */
