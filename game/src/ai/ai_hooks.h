/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-700 rule 7 (frame order), SPEC-760 B8.1, B9.3
 * Owner: package G2. The calls G18's files make into the AI (one call each, marked with a G2 comment there).
 * Declarations only, so that G18's files need not include the whole AI interface (ai_api.h). Vector parameters
 * are written as arrays (as vec3_t): MSVC decorates `const float x[3]` differently from `const float *x`.
 */
#ifndef OM_AI_HOOKS_H
#define OM_AI_HOOKS_H

struct edict_s;

namespace om_ai {
void LevelStart(void);                    /* SpawnEntities, before the map entities spawn */
void FrameBegin(void);                    /* G_RunFrame, before the entity loop */
void EntityGroundLost(struct edict_s *e); /* G_RunFrame entity loop: the ground entity's link count changed */
void PathCornerSpawn(struct edict_s *e);  /* SP_path_corner / SP_point_combat, spawn keys still in `st` */
/* stimuli (SPEC-700 "Noise broadcast", SPEC-701 sections 4 and 6, SPEC-710 E1) */
void ExplosionStimulus(const float center[3], float radius, float strength); /* G_ExplosionStimulus */
void NoiseAlert(struct edict_s *source, const float where[3], float radius); /* G_ReportNoise: alert sound kind 2 */
void FlashMute(const float origin[3], float radius);                        /* WhiteOut */
void ShotAtNotice(struct edict_s *shooter, const float start[3], const float end[3]); /* a bullet hit the world */
/* savegames (SPEC-752 1.2 step 6), called by G10's level save/load after the script chunks, before
 * GG_WriteAttached / GG_ReadAttached; see ai_api.h */
void WriteLevelAI(void);
void ReadLevelAI(void);
}

#endif
