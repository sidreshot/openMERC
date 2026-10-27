// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — savegame support of the per-prop state (public interface for G10).
//
// The prop state that the original module keeps in its entity records lives in G7's per-entity table
// (props::PropData, props.h). It is written and read as one OpenMerc extension record of the level section:
//  - props::WriteLevelProps(): chunk "G7PN" (int count), then per prop chunk "G7PD" (one record: entity number and
//    the PropData fields; static tables and callbacks through G10_EncodePtr, entities through G10_EncodeEnt, the two
//    Ghoul instance references inst0/inst1 as the index of the entity's tracked bolt-on part (G9's GGAttached),
//    0 = none, -1 = the entity's own model instance).
//  - props::ReadLevelProps(): clears the whole table (the spawned level's prop data must not survive a load), reads
//    the records and resolves inst0/inst1 again through the restored tracked parts — so no Ghoul instance pointer of
//    the destroyed spawn instances survives a load (turrets, searchlights).
// Both are registered with G10 as extension "G7PD" (static G10Extension in prop_save.cpp); G10 calls them through its
// extension list after the entities, AI block and Ghoul attached-object records are restored. G10 does not need to
// call them itself.
//
// Implements: SPEC-752 2.1 (encodings) for the G7 part of the level section; SPEC-770 §2 (sequence reference)
#pragma once

#include "props.h"

namespace props {
void WriteLevelProps(void);
void ReadLevelProps(void);
} // namespace props
