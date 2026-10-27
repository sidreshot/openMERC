/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-143, SPEC-740 (Amendments to SPEC-143, rules 2 and 14.6, Constants)
 * Owner: package G5 (game/src/nav).
 *
 * Engine-independent part of the navigation runtime: the in-memory form of a `.nvb` file, its parser and
 * writer, the grid lookup (point -> layer, SPEC-740 rule 2) and the routing-table rule of the export pipeline
 * (SPEC-740 rule 14.6). Nothing here touches the engine, so the same code is built into the stand-alone
 * checker game/src/nav/test/navcheck.cpp, which recomputes the tables of every stock file.
 */
#ifndef OM_NAV_DATA_H
#define OM_NAV_DATA_H

#include <stddef.h>
#include <stdint.h>

namespace om_nav {

enum {
	NVB_VERSION = 40,
	NAV_UNREACHABLE = 16000,
	NAV_FLEE_RANK_LIMIT = 15000,
	NAV_LAYER_NODES = 12,
	NAV_CELL = 48,
	NAV_SEEN_MAX = 1024
};

/* node flags (SPEC-143; editor commands of SPEC-740 rule 12) */
enum {
	NF_POINT = 0x1,          /* setpoint */
	NF_FLEE = 0x4,           /* setflee: general flee point */
	NF_FLEE_FRIENDLY = 0x8000,
	NF_FLEE_ENEMY = 0x10000,
	NF_BODY_MASK = NF_POINT | NF_FLEE | NF_FLEE_FRIENDLY | NF_FLEE_ENEMY
};

struct NavLink {
	int32_t target;
	float length;
	int32_t type; /* always 0 */
};

struct NavNode {
	int32_t flags;
	int32_t index;
	float origin[3];   /* as stored; the run-time loader raises z by 14 (NavData::origin_raised) */
	bool body;         /* record carries links and routing tables (flags & NF_BODY_MASK) */
	int32_t nlinks;
	NavLink *links;
	uint8_t *next_link; /* [count of the node's set] 1-based link index of the first step, 0 = none */
	int16_t *rank;      /* [count of the node's set] */
	int32_t nlinks2;    /* second link list (editor "combat point" links), loaded but unused */
	NavLink *links2;
	float clearance;    /* tail1 */
	int32_t tail2;      /* nearest general / friendly-only flee node */
	int32_t tail3;      /* nearest enemy-only / second general flee node */
	int32_t ignore;     /* tail4: never used as a world-spawn spot */
	int32_t tail5, tail6;
};

#pragma pack(push, 1)
struct NavLayer {
	float pos[3];       /* x, y (= column/cell), floor z */
	uint8_t flags;
	int16_t id;
	int16_t nodes[NAV_LAYER_NODES]; /* -1 = unused, filled from the front */
	int16_t nbr[4];     /* 0 = -y, 1 = +x, 2 = +y, 3 = -x; -1 = none */
	float top;
};
#pragma pack(pop)
static_assert(sizeof(NavLayer) == 51, "SPEC-143 grid layer is 51 bytes");

struct NavCell {
	float y;
	int32_t nlayers;
	NavLayer *layers;
};

struct NavColumn {
	float x;
	int32_t ncells;
	NavCell *cells;
};

struct NavData {
	int32_t version;      /* first int as stored */
	int32_t n;            /* node count N */
	NavNode *nodes;
	int32_t m;            /* second node set ("combat points"), 0 in stock files */
	NavNode *nodes2;
	int32_t nlayers;      /* L */
	int32_t ncolumns;     /* C */
	NavColumn *columns;
	NavLayer **layer_by_id; /* [L], NULL entries for ids that do not occur */
	bool origin_raised;
};

/* Parse a .nvb image. The node and link content is not validated (SPEC-740 rule 1); the parser only refuses to
 * read past the end of the buffer. Returns false with a message on a truncated file. The version is *not*
 * checked here (the caller compares the low byte with 40). */
bool ParseNvb(const uint8_t *buf, size_t len, NavData *out, char *err, size_t errsize);
void FreeNvb(NavData *nav);

/* Serialise to the SPEC-143 layout (with the SPEC-740 amendments). Returns the byte size; with out == NULL (or
 * cap too small) only the size is computed. Origins are written as stored (without the +14 raise). */
size_t WriteNvb(const NavData *nav, uint8_t *out, size_t cap);

/* Raise every node origin z by 14 (SPEC-740 Amendments: "On load ..."). Idempotent per NavData. */
void RaiseOrigins(NavData *nav);

/* SPEC-740 rule 2: the layer containing point p, or NULL. */
const NavLayer *LookupLayer(const NavData *nav, const float p[3]);

/* SPEC-740 rule 14.6: recompute next_link, rank, tail2 and tail3 of nodes[0..n-1] (the tables of each node must
 * be allocated with n entries) from the stored links. Works on the main set. */
void ComputeRoutes(NavNode *nodes, int n);

/* SPEC-740 rule 5: node choice on a layer towards target node t; returns the node index (may be < 1, the caller
 * maps it) or -1 for none. flee = false: smallest rank_t; flee = true: largest rank_t below 15000. */
int ChooseNodeOnLayer(const NavData *nav, const NavLayer *layer, int t, bool flee);

} // namespace om_nav

#endif
