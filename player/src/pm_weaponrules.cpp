/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - per-weapon rules: sequence choice by stance, idle choice, per-think rules (stances,
 * release of automatic weapons, sniper zoom), end-of-sequence chains, shot gates, visible rounds, looping sound.
 * Implements: SPEC-600 (Behaviour 10, Constants 3), SPEC-601 (variables used by the rules)
 */
#include <stdio.h>
#include <string.h>
#include "pm_local.h"

namespace om {

static bool is(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static bool is_transition(const char *seq) { return seq && strstr(seq, "_to_") != NULL; }

/* ---------------------------------------------------------------------------------------------- */
/* sequence names                                                                                 */
/* ---------------------------------------------------------------------------------------------- */
const char *Inventory::PrimarySeq()
{
    switch (W()) {
    case W_KNIFE: return PRandom(0, 100) >= 75 ? "fire1_1a" : "fire1_2a";
    case W_PISTOL1: return st.p1Stance == 1 && !st.subPose ? "fire1_a" : "fire1_b";
    case W_PISTOL2: return st.p2Stance ? "fire1_c" : "fire1_a";
    case W_MPISTOL: return "fire2_1";
    case W_ASSAULT: return st.stanceB ? "fire1_b" : "fire1_a";
    case W_SNIPER: return "fire1";
    case W_SLUGGER: return "fire1";
    case W_SHOTGUN: return "fire1";
    case W_MACHINEGUN: return "fire1_1";
    case W_ROCKET: return "fire1";
    case W_MPG: return "fire2";
    case W_FLAMEGUN: return "fire1_1";
    default: return NULL;
    }
}

const char *Inventory::AltSeq()
{
    switch (W()) {
    case W_KNIFE: return "fire2_1";
    case W_SLUGGER: return "fire2";
    case W_MACHINEGUN: return "fire2";
    case W_ROCKET: return "fire2";
    case W_MPG: return "fire1";
    case W_FLAMEGUN: return "fire2_1";
    default: return NULL;
    }
}

const char *Inventory::ReloadSeq()
{
    switch (W()) {
    case W_PISTOL1: return st.p1Stance == 1 && !st.subPose ? "reload_a" : "reload_b";
    case W_PISTOL2: return st.p2Stance ? "reload_c" : "reload_a";
    case W_ASSAULT: return st.stanceB ? "reload_b" : "reload_a";
    case W_SHOTGUN: return "reload_1";
    case W_MPISTOL:
    case W_SNIPER:
    case W_SLUGGER:
    case W_MACHINEGUN:
    case W_ROCKET:
    case W_MPG: return "reload";
    case W_FLAMEGUN: return "reload_a";
    default: return NULL;
    }
}

const char *Inventory::PutawaySeq()
{
    switch (W()) {
    case W_PISTOL1: return st.p1Stance == 1 && !st.subPose ? "done_a" : "done_b";
    case W_PISTOL2: return st.p2Stance ? "done_c" : "done_a";
    case W_ASSAULT: return st.stanceB ? "done_b" : "done_a";
    case W_SNIPER: return st.stanceB ? "done_a" : "done_b";
    case W_KNIFE:
    case W_MPISTOL:
    case W_SLUGGER:
    case W_SHOTGUN:
    case W_MACHINEGUN:
    case W_ROCKET:
    case W_MPG:
    case W_FLAMEGUN: return "done";
    default: return NULL;
    }
}

const char *Inventory::DrySeq()
{
    switch (W()) {
    case W_PISTOL1: return st.p1Stance == 1 && !st.subPose ? "dryfire_1hand" : "dryfire_2hands";
    case W_PISTOL2: return st.p2Stance ? "dryfire_lhand" : "dryfire_rhand";
    case W_ASSAULT: return st.stanceB ? "dryfire_side" : "dryfire_up";
    case W_KNIFE: return NULL;
    default: return W() > 0 && W() < W_HURTHAND ? "dryfire" : NULL;
    }
}

const char *Inventory::DrawSeq()
{
    switch (st.animWeapon) {
    case W_PISTOL1:
    case W_PISTOL2: return "ready_a";
    case W_HURTHAND: return "hurthand";
    case W_THROWHAND: return "throwhand";
    default: return st.animWeapon > 0 ? "ready" : NULL;
    }
}

/* ---------------------------------------------------------------------------------------------- */
/* idle choice (SPEC-600 Behaviour 10, common idle rule)                                          */
/* ---------------------------------------------------------------------------------------------- */
void Inventory::ChooseIdle()
{
    if (!st.inst)
        return;
    int w = st.animWeapon;
    if (w <= 0 || w >= W_HURTHAND)
        return;
    IdleRule();
}

void Inventory::IdleRule()
{
    const int w = st.animWeapon;
    const int r = PRandom(0, w == W_SLUGGER ? 130 : 100);
    /* the counter blocks variations while non-zero; a plain choice lowers it by one, not below 0
       (SPEC-AMEND-006 I7.2) */
    const int saved = st.idleCounter;
    const bool plain = st.idleCounter != 0;
    if (plain)
        st.idleCounter = st.idleCounter > 0 ? st.idleCounter - 1 : 0;
    const bool lp = st.ctx && st.ctx->leanPositive;
    const bool ln = st.ctx && st.ctx->leanNegative;
    const char *seq = "idle";
    bool variation = false, transition = false;

    switch (w) {
    case W_KNIFE:
        if (!plain) {
            if (r < 20)
                seq = "idlefinger_1", variation = true;
            else if (r < 40)
                seq = "idlefinger_2", variation = true;
            else if (r < 60 && st.knifeBloody)
                seq = "idleclean", variation = true;
        }
        break;
    case W_PISTOL1: {
        const bool a = st.p1Stance == 1, c = st.subPose != 0;
        if (lp && (a || c)) {
            seq = "idle_a_to_idle_b", transition = true;
            st.p1Stance = 2, st.subPose = 0;
        } else if (ln && !a && !c) {
            seq = "idle_b_to_idle_a", transition = true;
            st.p1Stance = 1;
        } else if (plain) {
            seq = c ? "idle_c" : a ? "idle_a" : "idle_b";
        } else if (c) {
            if (r < 30 && !st.transitionLast && !lp && !ln) {
                seq = "idle_c_to_idle_b", transition = true;
                st.p1Stance = 2, st.subPose = 0;
            } else if (r > 89)
                seq = "idlefinger_c", variation = true;
            else
                seq = "idle_c";
        } else if (a) {
            if (r < 20 && !st.transitionLast && !lp && !ln) {
                seq = "idle_a_to_idle_b", transition = true;
                st.p1Stance = 2;
            } else if (r > 79)
                seq = "idlefinger_a", variation = true;
            else
                seq = "idle_a";
        } else {
            if (r < 10 && !ln && !st.transitionLast) {
                seq = "idle_b_to_idle_c", transition = true;
                st.subPose = 1;
            } else if (r < 30 && r >= 10 && !ln && !st.transitionLast) {
                seq = "idle_b_to_idle_a", transition = true;
                st.p1Stance = 1;
            } else if (r > 94)
                seq = "idlefinger_b", variation = true;
            else
                seq = "idle_b";
        }
        st.transitionLast = transition ? 1 : 0;
        break;
    }
    case W_PISTOL2: {
        const bool c = st.p2Stance != 0, d = st.subPose != 0;
        if (lp && !c && !d) {
            seq = "idle_a_to_idle_c", transition = true;
            st.p2Stance = 1;
        } else if (ln && !lp && c) {
            seq = "idle_c_to_idle_a", transition = true;
            st.p2Stance = 0;
        } else if (plain) {
            seq = c ? "idle_c" : d ? "idle_d" : "idle_a";
        } else if (c) {
            if (r < 80)
                seq = "idle_c";
            else
                seq = "idlefinger_c", variation = true;
        } else if (d) {
            if (r < 10 && !st.transitionLast) {
                seq = "idle_d_to_idle_a", transition = true;
                st.subPose = 0;
            } else
                seq = "idle_d";
        } else {
            if (r < 10 && !st.transitionLast) {
                seq = "idle_a_to_idle_d", transition = true;
                st.subPose = 1;
            } else if (r < 90)
                seq = "idle_a";
            else
                seq = "idlefinger_a", variation = true;
        }
        st.transitionLast = transition ? 1 : 0;
        break;
    }
    case W_MPISTOL:
        if (!plain && r < 20)
            seq = "idlefinger_a", variation = true;
        break;
    case W_ASSAULT: {
        const bool b = st.stanceB != 0;
        if (ln && b) {
            seq = "idle_b_to_idle_a", transition = true;
            st.stanceB = 0;
        } else if (lp && !ln && !b) {
            seq = "idle_a_to_idle_b", transition = true;
            st.stanceB = 1;
        } else if (plain) {
            seq = b ? "idle_b" : "idle_a";
        } else if (b) {
            if (r < 30 && !st.transitionLast) {
                seq = "idle_b_to_idle_a", transition = true;
                st.stanceB = 0;
            } else if (r < 50)
                seq = "idlefinger_b", variation = true;
            else
                seq = "idle_b";
        } else {
            if (r < 10 && !st.transitionLast) {
                seq = "idle_a_to_idle_b", transition = true;
                st.stanceB = 1;
            } else if (r < 20)
                seq = "idlefinger_a", variation = true;
            else
                seq = "idle_a";
        }
        st.transitionLast = transition ? 1 : 0;
        break;
    }
    case W_SNIPER:
        /* the gate is block 0x260 ("drawn", set by the draw and the reload start; SPEC-AMEND-006 I7.12): before
           that the plain idle is chosen without touching the variation counter */
        if (!st.stanceB) {
            if (plain)
                st.idleCounter = saved;
            Play("idle_a");
            return;
        }
        seq = "idle_a";
        if (!plain && r < 20)
            seq = "idlefinger", variation = true;
        break;
    case W_SLUGGER:
        /* SPEC-AMEND-006 I7.15: while the sequence-name word is exactly `ready` the choice is `ready_idle`, a
           variation (counter := 2) whatever the counter was */
        if (!strcmp(st.seqName, "ready"))
            seq = "ready_idle", variation = true;
        else if (!plain) {
            if (r <= 9)
                seq = "idlefinger_a", variation = true;
            else if (r <= 24)
                seq = "idlefinger_b", variation = true;
        }
        break;
    case W_SHOTGUN:
        if (!plain && r < 10)
            seq = "idlefinger_a", variation = true;
        break;
    case W_MACHINEGUN:
        if (!plain) {
            if (r < 20)
                seq = "idlefinger_a", variation = true;
            else if (r < 40)
                seq = "idlefinger_b", variation = true;
        }
        break;
    case W_ROCKET:
        if (!plain) {
            if (r < 10)
                seq = "idlefinger_a", variation = true;
            else if (r < 20)
                seq = "idlefinger_b", variation = true;
        }
        break;
    case W_MPG:
        if (!plain) {
            if (r < 15)
                seq = "idlefinger_a", variation = true;
            else if (r < 30)
                seq = "idlefinger_b", variation = true;
        }
        break;
    case W_FLAMEGUN:
        FlameSound(false);
        if (!plain) {
            if (r < 10)
                seq = "idlefinger_a", variation = true;
            else if (r < 20)
                seq = "idlefinger_b", variation = true;
            else if (r < 30)
                seq = "idlefinger_c", variation = true;
        }
        break;
    default: return;
    }
    if (variation || transition)
        st.idleCounter = 2;
    Play(seq);
}

/* ---------------------------------------------------------------------------------------------- */
/* per-think rules                                                                                */
/* ---------------------------------------------------------------------------------------------- */
static void stance_change(Inventory *inv, const char *seq)
{
    inv->st.idleCounter = 2;
    inv->st.transitionLast = 1;
    inv->Play(seq);
}

void Inventory::PerThinkRule(int &att, int &alt, int x1, int x2)
{
    const int w = W();
    const bool ready = st.ready > 0;
    const char *cur = st.seqName;
    switch (w) {
    case W_KNIFE:
        if (ready) {
            if (x1 && !is(cur, "idlefinger_1") && !is(cur, "idleclean"))
                Play("idlefinger_1");
            else if (x2 && !is(cur, "idleclean") && !is(cur, "idlefinger_1"))
                Play("idleclean");
        }
        break;
    case W_PISTOL1:
        if (ready && !is_transition(cur)) {
            const bool a = st.p1Stance == 1;
            if (x1 && (a || st.subPose)) {
                st.p1Stance = 2, st.subPose = 0;
                stance_change(this, "idle_a_to_idle_b");
            } else if (x2 && !a && !st.subPose) {
                st.p1Stance = 1;
                stance_change(this, "idle_b_to_idle_a");
            }
        }
        break;
    case W_PISTOL2:
        if (ready && !is_transition(cur)) {
            if (x1 && !st.p2Stance) {
                st.p2Stance = 1, st.subPose = 0;
                stance_change(this, "idle_a_to_idle_c");
            } else if (x2 && st.p2Stance) {
                st.p2Stance = 0;
                stance_change(this, "idle_c_to_idle_a");
            }
        }
        break;
    case W_ASSAULT:
        if (st.wstate == WS_FIRE1 && !att) {
            GoIdle();
            break;
        }
        if (ready && !is_transition(cur)) {
            if (x1 && !st.stanceB) {
                st.stanceB = 1;
                stance_change(this, "idle_a_to_idle_b");
            } else if (x2 && st.stanceB) {
                st.stanceB = 0;
                stance_change(this, "idle_b_to_idle_a");
            }
        }
        break;
    case W_MPISTOL:
    case W_MACHINEGUN:
        /* releasing the button goes idle at once; the spread counter is kept */
        if (st.wstate == WS_FIRE1 && !att)
            GoIdle();
        break;
    case W_SHOTGUN:
        if (ready && !is(cur, "idlefinger_b") && !is(cur, "idlefinger_c")) {
            if (x1)
                Play("idlefinger_b");
            else if (x2)
                Play("idlefinger_c");
        }
        break;
    case W_MPG:
        if (st.wstate == WS_FIRE1 && !att) {
            GoIdle();
            Play("fire2_end");
        }
        break;
    case W_FLAMEGUN:
        if (st.wstate == WS_FIRE1 && !att) {
            GoIdle();
            Play("fire1_2", 0);
            FlameSound(false);
        }
        break;
    case W_SNIPER: SniperZoomThink(alt, x1, x2); break;
    default: break;
    }
}

/* sniper scope (SPEC-600 Behaviour 10, sniper rifle) */
void Inventory::SniperZoomThink(int alt, int x1, int x2)
{
    if (!st.scoped)
        return;
    OwnerContext *c = st.ctx;
    float fov = GetFov();
    if (c && c->remoteView) {
        if (fov < 95.f) {
            SetFov(95.f);
            ScopeModel(false);
            st.scoped = 0;
        }
        return;
    }
    if (st.zoomHeld && !alt)
        st.zoomHeld = 0;
    int dir = 0;
    if (x1 && !x2)
        dir = -1;
    else if (x2 && !x1)
        dir = 1;
    else if (!x1 && !x2 && st.zoomHeld && alt)
        dir = -1;
    if (!dir)
        return;
    float step = 5.f * (c ? c->frameTen : 1.f);
    fov += dir * step;
    if (fov < 6.f)
        fov = 6.f;
    if (fov > 60.f)
        fov = 60.f;
    SetFov(fov);
    st.savedZoom = (int)fov;
}

/* ---------------------------------------------------------------------------------------------- */
/* end-of-sequence chains                                                                         */
/* ---------------------------------------------------------------------------------------------- */
bool Inventory::EndRule(const char *seq)
{
    const int w = W();
    switch (w) {
    case W_KNIFE:
        if (is(seq, "fire1_1a")) {
            Play("fire1_1b");
            return true;
        }
        if (is(seq, "fire1_2a")) {
            st.spread = 0;
            int r = PRandom(0, 100);
            Play(r < 33 ? "fire1_2sr_to_sl" : r < 67 ? "fire1_2dr_to_ul" : "fire1_2ur_to_dl");
            return true;
        }
        if (is(seq, "fire2_1")) {
            IGhoulObject *o = st.inst ? st.inst->GetGhoulObject() : NULL;
            GhoulID mat = o ? o->FindMaterial("knife") : 0;
            GhoulID skin = mat ? o->FindSkin(mat, "knife", 0) : 0;
            if (mat && skin)
                st.inst->SetSkin(mat, skin, 0);
            st.knifeBloody = 0;
            Play("fire2_2");
            return true;
        }
        if (is(seq, "fire2_2") && Ammo() && st.ammo[AM_KNIFE] < 2) {
            GoIdle();
            return true;
        }
        if (is(seq, "fire1_2l_to_idle")) {
            Play("fire1_2a");
            return true;
        }
        return false;
    case W_PISTOL1:
    case W_PISTOL2:
    case W_ASSAULT:
        return false;
    case W_MPISTOL:
        if (is(seq, "fire2_1") && st.wstate == WS_FIRE1 && !WantsRefire(false)) {
            Play("fire2_2");
            return true;
        }
        return false;
    case W_SNIPER:
        if (is(seq, "scopein")) {
            SetFov(60.f);
            st.savedZoom = 60;
            st.scoped = 1;
            st.zoomHeld = 1;
            ScopeModel(true);
            GoIdle();
            return true;
        }
        if (is(seq, "scopeout")) {
            GoIdle();
            return true;
        }
        if (is(seq, "reload")) {
            /* "drawn" and 0x208 were set by the reload start (SPEC-AMEND-006 I7.12) */
            if (st.savedZoom) {
                SetFov((float)st.savedZoom);
                st.scoped = 1;
                st.zoomHeld = 1;
                ScopeModel(true);
            }
            GoIdle();
            return true;
        }
        if (is(seq, "ready"))
            st.stanceB = 1;
        return false;
    case W_SLUGGER:
        return false; /* the draw's end is an ordinary idle choice, which picks `ready_idle` (IdleRule) */
    case W_SHOTGUN: {
        /* shell by shell: every reload_loop end loads one shell (the weapon may fire again from then on) and
           loops; the loop that finds nothing to load is followed by reload_2 (observed on the 1.07f module) */
        WeaponSlot &s = CurSlot();
        const int size = kWeapons[W_SHOTGUN].clip;
        if (st.wstate != WS_RELOAD)
            return false;
        if (is(seq, "reload_1")) {
            Play("reload_loop");
            return true;
        }
        if (is(seq, "reload_loop")) {
            bool can = s.clip < size && (st.ammo[AM_SHELLS] > 0 || !Ammo()) && st.varC && !st.varE;
            if (can) {
                s.clip++;
                if (Ammo())
                    st.ammo[AM_SHELLS]--;
                st.ready = 1;
                WriteCounters();
                Play("reload_loop");
            } else
                Play("reload_2");
            return true;
        }
        return false;
    }
    case W_MACHINEGUN:
        if (is(seq, "fire1_1") && st.wstate == WS_FIRE1 && !WantsRefire(false)) {
            st.spread = 0;
            st.wstate = WS_IDLE;
            Play("fire1_2");
            return true;
        }
        return false;
    case W_MPG:
        if (is(seq, "fire1") && st.wstate == WS_FIRE2) {
            Play("charge");
            return true;
        }
        return false;
    case W_FLAMEGUN:
        if (is(seq, "fire2_1") && st.wstate == WS_FIRE2) {
            Play("fire2_2");
            return true;
        }
        if (is(seq, "fire2_2") && st.wstate == WS_FIRE2) {
            GoIdle();
            return true;
        }
        return false;
    default: return false;
    }
}

/* the sequence started by a refire (SPEC-600 Behaviour 7 and the weapons' "after refire" rules) */
const char *Inventory::RefireSeq(bool alt)
{
    const int w = W();
    if (w == W_KNIFE && !alt) {
        int r = PRandom(0, 100);
        if (st.spread & 1)
            return r < 33 ? "fire1_2sl_to_sr" : r < 67 ? "fire1_2dl_to_ur" : "fire1_2ul_to_dr";
        return r < 33 ? "fire1_2sr_to_sl" : r <= 66 ? "fire1_2dr_to_ul" : "fire1_2ur_to_dl";
    }
    if (w == W_MPG && !alt)
        return "fire2_loop";
    if (w == W_FLAMEGUN && !alt)
        return "fire1_idle";
    return alt ? AltSeq() : PrimarySeq();
}

/* ---------------------------------------------------------------------------------------------- */
/* shot gates and visible rounds                                                                  */
/* ---------------------------------------------------------------------------------------------- */
/* Shot gates (SPEC-600 Behaviour 10). The shot time is recorded before the round is paid; a note that cannot be
   paid starts the automatic reload at once (observed; SPEC-600 test vector 8). */
bool Inventory::ShotGate(bool alt)
{
    const int w = W();
    const float now = Now();
    const bool automatic = (!alt && (w == W_MPISTOL || w == W_ASSAULT)) || (alt && w == W_ROCKET);
    if (automatic) {
        if (now == st.lastAutoShot)
            return false;
        st.lastAutoShot = now;
        if (st.prepaid)
            st.prepaid = 0;
        else {
            if (!TakeShot(alt, true))
                return false;
            st.spread++;
            VisibleRounds(false, true);
        }
        return true;
    }
    if (w == W_ROCKET) {
        /* single primary rocket: the shot time is recorded (its round was paid at the attack start) */
        if (now == st.lastAutoShot)
            return false;
        st.lastAutoShot = now;
        return true;
    }
    if (w == W_MACHINEGUN) {
        /* fire and altfire notes share the 0.195 s gate; the phosphorus grenade records the time and never pays
           (SPEC-AMEND-006 I7.10) */
        if (now - st.lastAutoShot < 0.195f)
            return false;
        st.lastAutoShot = now;
        if (alt || st.wstate != WS_FIRE1)
            return true;
        if (st.prepaid)
            st.prepaid = 0;
        else {
            if (!TakeShot(false, true))
                return false;
            st.spread++;
            VisibleRounds(false, true);
        }
        return true;
    }
    return true;
}

static void set_part(IGhoulInst *inst, const char *name, bool on)
{
    IGhoulObject *o = inst ? inst->GetGhoulObject() : NULL;
    GhoulID p = o ? o->FindPart(name) : 0;
    if (p)
        inst->SetPartOnOff(p, on);
}

void Inventory::VisibleRounds(bool onSelect, bool paidShot)
{
    const int w = st.animWeapon;
    if (!st.inst || (w != W_ASSAULT && w != W_MACHINEGUN))
        return;
    const int c = CurrentClip();
    char name[16];
    const bool onlyOne = paidShot && !IsClient();
    if (w == W_ASSAULT) {
        if (c < 3 || c > 15 || (!onSelect && !paidShot))
            return;
        for (int i = c + 1; i <= 16; i++) {
            _snprintf(name, sizeof(name), "%d", i);
            set_part(st.inst, name, false);
            if (onlyOne)
                break;
        }
    } else {
        if (c < 0 || c > 7 || (!onSelect && !paidShot))
            return;
        for (int i = c + 1; i <= 8; i++) {
            _snprintf(name, sizeof(name), "BULLET%d", i);
            set_part(st.inst, name, false);
            if (onlyOne)
                break;
        }
    }
}

void Inventory::ShellBullets()
{
    const int w = st.animWeapon;
    if (!st.inst)
        return;
    char name[16];
    if (w == W_ASSAULT) {
        int top = Ammo() ? CurrentClip() : 16;
        if (top > 16)
            top = 16;
        for (int i = 4; i <= top; i++) {
            _snprintf(name, sizeof(name), "%d", i);
            set_part(st.inst, name, true);
        }
    } else if (w == W_MACHINEGUN) {
        int top = Ammo() ? CurrentClip() : 8;
        if (top > 8)
            top = 8;
        for (int i = 1; i <= top; i++) {
            _snprintf(name, sizeof(name), "BULLET%d", i);
            set_part(st.inst, name, true);
        }
    }
}

/* ---------------------------------------------------------------------------------------------- */
/* deselect, fov, scope model, looping sound                                                      */
/* ---------------------------------------------------------------------------------------------- */
void Inventory::DeselectRule()
{
    switch (W()) {
    case W_SNIPER:
        /* the model stays hidden through the put-away (observed); the next weapon gets a new instance */
        if (st.scoped || GetFov() != 95.f)
            SetFov(95.f);
        st.scoped = 0;
        st.zoomHeld = 0;
        st.savedZoom = 0;
        break;
    case W_FLAMEGUN: FlameSound(false); break;
    default: break;
    }
}

void Inventory::SetFov(float fov)
{
    unsigned char *ps = PS();
    if (ps)
        *(float *)(ps + PS_FOV) = fov;
}

float Inventory::GetFov()
{
    unsigned char *ps = PS();
    return ps ? *(float *)(ps + PS_FOV) : 95.f;
}

void Inventory::ScopeModel(bool hidden)
{
    if (st.inst)
        st.inst->SetAllPartsOnOff(!hidden);
}

void Inventory::FlameSound(bool on)
{
    if (IsClient() || !g_haveSv)
        return;
    edict_t *e = Owner();
    if (!e)
        return;
    int *sound = (int *)((char *)e + EDICT_LOOP_SOUND);
    int *data = (int *)((char *)e + EDICT_LOOP_DATA);
    if (on) {
        *sound = g_sv.SoundIndex ? g_sv.SoundIndex("weapons/flamegun/fire.wav") : 0;
        *data = 248;
    } else if (g_sv.CarriesFlag && g_sv.CarriesFlag(e)) {
        *sound = g_sv.SoundIndex ? g_sv.SoundIndex("player/flaglp.wav") : 0;
        *data = 248;
    } else {
        *sound = 0;
        *data = 0;
    }
}

/* slot 63: landing (machinegun) */
void Inventory::Landed()
{
    if (W() == W_MACHINEGUN && st.ready > 0)
        Play("land", 0);
}

/* slot 34: knife blood */
void Inventory::KnifeBlood(int arg)
{
    if (st.knifeBloody)
        return;
    st.knifeBloody = 1;
    if (W() == W_KNIFE && st.inst) {
        IGhoulObject *o = st.inst->GetGhoulObject();
        GhoulID mat = o ? o->FindMaterial("knife") : 0;
        GhoulID skin = mat ? o->FindSkin(mat, "knifeblood", 0) : 0;
        if (mat && skin)
            st.inst->SetSkin(mat, skin, 0);
    }
    if (!IsClient())
        QueueEvent(4, arg);
}

} /* namespace om */
