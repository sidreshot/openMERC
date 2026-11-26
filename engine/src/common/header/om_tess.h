/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Tessellation class, weapon scale, and the view-weapon slot.
 * No engine services. Callers pass the mesh, the scale, and the entity count.
 *
 * Two meshes with the same name and the same normals get the same class.
 * A weapon scale is 1..10. The PN offset at 10 stays within OM_WEAPON_PN_EDGE
 * of the triangle edge, so the shotgun does not swell off its texture.
 * The view weapon keeps a slot at every yaw, including when the entity list is full.
 */

#ifndef OM_TESS_H
#define OM_TESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Fraction of the triangle edge the weapon surface may leave the flat triangle.
   A larger step walks the shotgun texels off the island and inflates the mesh. */
#define OM_WEAPON_PN_EDGE 0.12f

/* 1 when name contains any list entry (case insensitive). An empty entry matches nothing.
   Two calls with the same name and the same list return the same bit. */
int OM_TessNameRound(const char *name, const char *const *list, int nlist);

/* 1 when the vertices are a faceted round shell (a barrel: six or more horizontal
   directions, most of the vertices in that ring, no single direction dominating).
   A box has four directions and returns 0. The same normals return the same bit.
   nverts is the number of xyz triples. Above 768 vertices this returns 0: that is a
   character, not a prop shell. */
int OM_TessNormalRound(const float *xyz, int nverts);

/* Name list or the normal ring. Same inputs, same result, including a second mesh. */
int OM_TessClass(const char *name, const char *const *list, int nlist, const float *xyz, int nverts);

/* Weapon scale. Values outside 1..10 clamp onto that range. */
int OM_WeaponTessLevel(float requested);

/* Subdivision used for that scale. 2, 3, or 4 (the tessellation table holds 4). */
int OM_WeaponSubdiv(int level);

/* How far a weapon vertex moves from the flat triangle toward the PN point, 0..1.
   Level 10 stays far below 1: full PN is what swells the shotgun. */
float OM_WeaponPnK(int level);

/* out = clamped (k * delta). Length is at most OM_WEAPON_PN_EDGE * edge.
   edge <= 0 writes a zero offset. */
void OM_WeaponOffset(int level, float edge, const float delta[3], float out[3]);

/* How far a tessellated point may leave the flat triangle.
   e01, e12, e20 are the edge lengths P1-P2, P2-P3, P3-P1. u, v, w are the
   barycentric weights of P1, P2, P3. A point on one edge uses that edge, so the
   triangle across the edge gets the same limit. An interior point uses the
   shortest edge. The limit is at most 1. cap <= 0 returns a huge limit. */
float OM_TessLimit(float cap, float e01, float e12, float e20, float u, float v, float w);

/* Index of the view weapon in a list that already holds count entities and has room
   for limit. A full list still returns a slot (the last one is overwritten).
   yaw is ignored: turning the camera does not move the weapon out of the list.
   limit < 1 returns -1. */
int OM_ViewWeaponSlot(int count, int limit, float yaw);

#ifdef __cplusplus
}
#endif

#endif /* OM_TESS_H */
