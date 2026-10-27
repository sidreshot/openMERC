/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - in-view weapon models: model objects (cached per weapon), precaching of their sounds,
 * effects and attacks, instance set-up and the note callbacks the Ghoul runtime calls.
 * Implements: SPEC-600 (Behaviour 12, notes), SPEC-003 (exports 7/8, common import 17, client imports 4/6),
 *             SPEC-010 (model set-up, event callback, note enumeration), SPEC-011 (instance)
 */
#include <stdio.h>
#include <stdlib.h>
#include "pm_local.h"

namespace om {

/* ------------------------------------------------------------------------------------------------ */
/* note text helpers                                                                                */
/* ------------------------------------------------------------------------------------------------ */

/* "name [n|*]": copies the name and returns the count (0 = plain name, -1 = '*') */
static int split_note(const char *text, char *name, int size)
{
    int i = 0;
    while (text[i] && text[i] != ' ' && i < size - 1) {
        name[i] = text[i];
        i++;
    }
    name[i] = 0;
    const char *rest = text + i;
    while (*rest == ' ')
        rest++;
    if (*rest == '*')
        return -1;
    if (*rest >= '0' && *rest <= '9')
        return atoi(rest);
    return 0;
}

static void sprintf_via_import(char *dst, int size, const char *fmt, const char *s, int n)
{
    if (g_com.Com_sprintf)
        g_com.Com_sprintf(dst, size, fmt, s, n);
    else
        _snprintf(dst, size, fmt, s, n);
}

/* precache enumeration (object method 14): sounds and effects named by notes */
class SoundPrecache : public IGhoulCallback {
public:
    bool Event(uint32_t, void *, float, const void *data) override
    {
        if (!data || IsClient() || !g_haveSv || !g_sv.SoundIndex)
            return true;
        char name[256], file[256];
        int n = split_note((const char *)data, name, sizeof(name));
        if (n > 0) {
            for (int i = 1; i <= n; i++) {
                sprintf_via_import(file, sizeof(file), "%s%d.wav", name, i);
                g_sv.SoundIndex(file);
            }
        } else {
            sprintf_via_import(file, sizeof(file), "%s.wav", name, 0);
            g_sv.SoundIndex(file);
        }
        return true;
    }
};

class EffectPrecache : public IGhoulCallback {
public:
    bool Event(uint32_t, void *, float, const void *data) override
    {
        if (!data || IsClient() || !g_haveSv || !g_sv.EffectIndex)
            return true;
        char name[256];
        split_note((const char *)data, name, sizeof(name));
        g_sv.EffectIndex(name);
        return true;
    }
};

static SoundPrecache s_soundPrecache;
static EffectPrecache s_effectPrecache;

/* ------------------------------------------------------------------------------------------------ */
/* note callbacks on view instances                                                                 */
/* ------------------------------------------------------------------------------------------------ */
enum { CB_SOUND, CB_FIRE, CB_ALTFIRE, CB_EOS, CB_CLEANBLOOD, CB_SHELLBULLETS, CB_EFFECT };
static const char *const kNoteNames[7] = {"sound", "fire", "altfire", "EOS", "cleanblood", "shellbullets",
                                          "effect"};

class NoteCallback : public IGhoulCallback {
public:
    explicit NoteCallback(int kind) : kind_(kind) {}
    bool Event(uint32_t, void *user, float, const void *data) override
    {
        Inventory *inv = (Inventory *)user;
        const char *arg = data ? (const char *)data : "";
        if (!inv)
            return true;
        switch (kind_) {
        case CB_SOUND:
            if (IsClient() && g_haveCl && g_cl.NoteSound)
                g_cl.NoteSound(arg);
            break;
        case CB_EFFECT:
            if (IsClient() && g_haveCl && g_cl.NoteEffect)
                g_cl.NoteEffect(arg);
            break;
        case CB_FIRE: inv->FireNote(false, arg); break;
        case CB_ALTFIRE: inv->FireNote(true, arg); break;
        case CB_EOS: inv->EndOfSequence(); break;
        case CB_CLEANBLOOD:
            if (inv->st.knifeBloody && inv->st.inst) {
                IGhoulObject *o = inv->st.inst->GetGhoulObject();
                GhoulID mat = o ? o->FindMaterial("knife") : 0;
                GhoulID skin = mat ? o->FindSkin(mat, "knife", 0) : 0;
                if (mat && skin)
                    inv->st.inst->SetSkin(mat, skin, 0);
                inv->st.knifeBloody = 0;
            }
            break;
        case CB_SHELLBULLETS: inv->ShellBullets(); break;
        }
        return true;
    }

private:
    int kind_;
};

static NoteCallback s_cb0(CB_SOUND), s_cb1(CB_FIRE), s_cb2(CB_ALTFIRE), s_cb3(CB_EOS), s_cb4(CB_CLEANBLOOD),
    s_cb5(CB_SHELLBULLETS), s_cb6(CB_EFFECT);
IGhoulCallback *g_noteCallbacks[7] = {&s_cb0, &s_cb1, &s_cb2, &s_cb3, &s_cb4, &s_cb5, &s_cb6};

/* ------------------------------------------------------------------------------------------------ */
/* model objects                                                                                    */
/* ------------------------------------------------------------------------------------------------ */
static bool specular_on()
{
    const char *cv = (const char *)g_com.ghlSpecular;
    return cv && *(const float *)(cv + 0x18) > 0.f;
}

static void precache_model_resources(int weapon, IGhoulObject *obj)
{
    if (IsClient())
        return;
    GhoulID n = obj->FindNote("sound");
    if (n)
        obj->EnumerateNotes(&s_soundPrecache, n);
    n = obj->FindNote("effect");
    if (n)
        obj->EnumerateNotes(&s_effectPrecache, n);
    const WeaponInfo &wi = kWeapons[weapon];
    if (g_haveSv && g_sv.PrecacheAttack) {
        if (wi.attack1)
            g_sv.PrecacheAttack(wi.attack1);
        if (wi.attack2)
            g_sv.PrecacheAttack(wi.attack2);
    }
    if (g_haveSv && g_sv.EffectIndex) {
        if (wi.muzzleFx)
            g_sv.EffectIndex(wi.muzzleFx);
        if (wi.muzzleFx2)
            g_sv.EffectIndex(wi.muzzleFx2);
    }
}

/* Creates and registers a model object (common import 17, registration finished with all skins). Only export 7
   precaches and sets the specular flag; a model made later for a selection, a predicted switch or the rebuild of
   export 11 gets neither (SPEC-AMEND-006 I7.16). The client keeps preparing its own objects for drawing. */
static IGhoulObject *create_model_object(const WeaponDesc *d, const char *dir, bool prepare)
{
    IGhoulSystem *gs = Ghoul();
    if (!gs)
        return NULL;
    IGhoulObject *obj = gs->CreateObject();
    if (!obj)
        return NULL;
    if (g_com.RegisterGsq)
        g_com.RegisterGsq(dir, d->dir, obj);
    obj->FinishRegistration(true);
    if (prepare)
        obj->Prepare(specular_on());
    return obj;
}

/* the cached model object of a weapon; created (and cached) when missing and create is set */
IGhoulObject *ViewModel(int weapon, bool create)
{
    if (weapon <= 0 || weapon >= NUM_WEAPONS)
        return NULL;
    WeaponDesc *d = g_desc[weapon];
    char dir[256];
    sprintf_via_import(dir, sizeof(dir), "Weapon/Inview/%s", d->dir, 0);
    if (d->model || !create)
        return d->model;
    d->model = create_model_object(d, dir, IsClient());
    return d->model;
}

/* SPEC-003 export 7 */
void PrecacheViewModels(unsigned mask)
{
    ServerContext server;
    for (int w = 1; w < NUM_WEAPONS; w++) {
        if (!(mask & (1u << w)))
            continue;
        WeaponDesc *d = g_desc[w];
        char dir[256];
        sprintf_via_import(dir, sizeof(dir), "Weapon/Inview/%s", d->dir, 0);
        if (d->model)
            continue;
        d->model = create_model_object(d, dir, true);
        if (d->model)
            precache_model_resources(w, d->model);
    }
}

/* SPEC-AMEND-006 I7.16: export 11 forgets the current weapon's cached object (not destroyed) */
void ForgetViewModel(int weapon)
{
    if (weapon > 0 && weapon < NUM_WEAPONS)
        g_desc[weapon]->model = NULL;
}

/* SPEC-003 export 8: forget the cached objects (their owner, the Ghoul system, destroys them) */
void ForgetViewModels()
{
    for (int w = 0; w < NUM_WEAPONS; w++)
        g_desc[w]->model = NULL;
}

/* SPEC-600 Behaviour 12: instance set-up */
/* The stock in-view models face -Y in model space; the module turns every new view instance by 90 degrees
   about Z (local X -> +Y, local Y -> -X), so that the model's front points along +X (observed on the 1.07f
   module: instance transform rows (0 1 0), (-1 0 0), (0 0 1), no translation). */
static const float kViewTurn[16] = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

/* skinTarget: the instance that gets the `arms` -> `arms_sleeve` skin, looked up in its own model. Normally the new
   instance itself; for the "next" instance of a predicted switch the original applies it to the current (old)
   instance instead and the next instance keeps its default `arms` skin (SPEC-AMEND-006 I7.4). */
void SetupViewInstance(IGhoulInst *inst, Inventory *inv, IGhoulInst *skinTarget)
{
    IGhoulObject *obj = inst->GetGhoulObject();
    if (obj) {
        GhoulID p;
        if ((p = obj->FindPart("_R_FOREARM")) != 0)
            inst->SetPartOnOff(p, false);
        if ((p = obj->FindPart("_L_FOREARM")) != 0)
            inst->SetPartOnOff(p, false);
        if ((p = obj->FindPart("_R_SLEEVE_FOREARM")) != 0)
            inst->SetPartOnOff(p, true);
        if ((p = obj->FindPart("_L_SLEEVE_FOREARM")) != 0)
            inst->SetPartOnOff(p, true);
    }
    if (!skinTarget)
        skinTarget = inst;
    IGhoulObject *sobj = skinTarget->GetGhoulObject();
    if (sobj) {
        GhoulID mat = sobj->FindMaterial("arms");
        if (mat) {
            GhoulID skin = sobj->FindSkin(mat, "arms_sleeve", 0);
            if (skin)
                skinTarget->SetSkin(mat, skin, 0);
        }
    }
    inst->SetUserValue(inv);
    if (obj) {
        for (int k = 0; k < 7; k++) {
            GhoulID note = obj->FindNote(kNoteNames[k]);
            if (note)
                inst->AddNoteCallback(g_noteCallbacks[k], note);
        }
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* inventory: instances and sequences                                                               */
/* ------------------------------------------------------------------------------------------------ */
void Inventory::DestroyInstance(IGhoulInst *&inst)
{
    if (inst)
        inst->Destroy();
    inst = NULL;
}

void Inventory::NewViewInstance(int weapon, bool asNext)
{
    /* the previous instance goes first (as on the original, whose new instance then reuses its memory) */
    IGhoulObject *obj = ViewModel(weapon, true);
    if (asNext) {
        /* after a predicted switch the next-instance word still names the current instance: not ours to destroy */
        if (x && x->nextOwned)
            DestroyInstance(st.nextInst);
    } else {
        if (st.nextInst == st.inst && x)
            x->nextOwned = false;
        DestroyInstance(st.inst);
    }
    IGhoulInst *inst = obj ? obj->CreateInstance() : NULL;
    PmTrace("NewViewInstance weapon %d next %d -> obj %p inst %d (current %d)", weapon, (int)asNext, obj,
            PmInstId(inst), PmInstId(st.inst));
    if (inst) {
        inst->SetTransform16(kViewTurn);
        SetupViewInstance(inst, this, (asNext && st.inst) ? st.inst : NULL);
    }
    if (asNext) {
        st.nextInst = inst;
        if (x)
            x->nextOwned = inst != NULL;
    } else
        st.inst = inst;
}

static void seq_path(char *out, int size, int weapon, const char *name)
{
    const char *dir = (weapon > 0 && weapon < NUM_WEAPONS) ? g_desc[weapon]->dir : "";
    if (g_com.Com_sprintf)
        g_com.Com_sprintf(out, size, "Ghoul/Weapon/Inview/%s/%s.ghl", dir, name);
    else
        _snprintf(out, size, "Ghoul/Weapon/Inview/%s/%s.ghl", dir, name);
}

bool Inventory::HasSeq(const char *name)
{
    if (!st.inst)
        return false;
    IGhoulObject *obj = st.inst->GetGhoulObject();
    if (!obj)
        return false;
    char path[256];
    seq_path(path, sizeof(path), st.animWeapon, name);
    return obj->FindSequence(path) != 0;
}

/* Starts a sequence on the current in-view instance (SPEC-600 Behaviour 2). mode 1 = play once, 0 = loop.
   start < 0 means now. Returns false (and plays nothing) when the model lacks the sequence. */
bool Inventory::Play(const char *name, int mode, float start)
{
    if (!st.inst || !name)
        return false;
    IGhoulObject *obj = st.inst->GetGhoulObject();
    if (!obj)
        return false;
    char path[256];
    seq_path(path, sizeof(path), st.animWeapon, name);
    GhoulID seq = obj->FindSequence(path);
    if (!seq)
        return false;
    float t = start < 0.f ? Now() : start;
    st.inst->Play(seq, t, 0.f, true, mode, false, false);
    Q_strncpyz(st.seqName, name, sizeof(st.seqName));
    return true;
}

} /* namespace om */
