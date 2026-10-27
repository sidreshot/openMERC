/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: fixed tables - surface materials and debris models.
 * Debris model file names are the stock Ghoul files of each group
 * (`ghoul/objects/generic/chunks_*`, pak0), matched to the SPEC-400 model list.
 *
 * Implements: SPEC-401 (Surface materials, Debris chunk types), SPEC-400
 * (Debris item 1)
 */

#include "fx_local.h"

/* sound-list entries (SPEC-401 client sound list) */
#define S_STONE 37
#define S_WATER 38
#define S_WOOD 39
#define S_GRASS 40
#define S_GRAVEL 41
#define S_SAND 42
#define S_SNOW 43
#define S_METAL 44 /* random of 79..81 */

#define F_METAL 2
#define F_SNOW 6
#define F_WATER 10
#define F_GRASS 14
#define F_GRAVEL 18
#define F_SAND 22
#define F_WOOD 26
#define F_STONE 30
#define F_FLESH 37 /* the stone impact sound for all four variants */

#define L_GRASS 71
#define L_GRAVEL 72
#define L_METAL 73
#define L_SAND 74
#define L_SNOW 75
#define L_STONE 76
#define L_WATER 77
#define L_WOOD 78

/* sprite-list entries */
#define H_STONE 13 /* bhole2 */
#define H_GLASS 33 /* bhole_glass3 */
#define H_METAL 42 /* bhole_mtl */
#define H_WOOD 43  /* bhole_wd */

#define STONE_ROW(r, g, b) {{r, g, b, 250}, 1, F_STONE, S_STONE, L_STONE, H_STONE, FXM_CONCRETE, NULL}
#define METAL_ROW(eff, hole, arg) {{250, 250, 250, 250}, 1, F_METAL, S_METAL, L_METAL, hole, eff, arg}

const fx_material_t fx_materials[FX_NUM_MATERIALS] = {
	/* 0 */ {{96, 96, 96, 250}, 1, F_STONE, S_STONE, L_STONE, H_STONE, FXM_CONCRETE, NULL},
	/* 1 */ METAL_ROW(FXM_METAL, H_METAL, NULL),
	/* 2 */ {{180, 140, 100, 250}, 2, F_SAND, S_SAND, L_SAND, H_STONE, FXM_DUST, NULL},
	/* 3 */ {{200, 200, 200, 250}, 2, F_SAND, S_SAND, L_SAND, H_STONE, FXM_DUST, NULL},
	/* 4 */ {{180, 130, 70, 250}, 2, F_SAND, S_SAND, L_SAND, H_STONE, FXM_DUST, NULL},
	/* 5 */ {{120, 90, 20, 250}, 2, F_SAND, S_SAND, L_SAND, H_STONE, FXM_DUST, NULL},
	/* 6 */ {{160, 160, 160, 250}, 1, F_GRAVEL, S_GRAVEL, L_GRAVEL, H_STONE, FXM_DUST, NULL},
	/* 7 */ {{120, 90, 20, 250}, 1, F_GRAVEL, S_GRAVEL, L_GRAVEL, H_STONE, FXM_DUST, NULL},
	/* 8 */ {{180, 140, 30, 250}, 1, F_GRAVEL, S_GRAVEL, L_GRAVEL, H_STONE, FXM_DUST, NULL},
	/* 9 */ {{230, 230, 230, 150}, 2, F_SNOW, S_SNOW, L_SNOW, H_STONE, FXM_DUST, NULL},
	/* 10 */ {{0, 200, 200, 250}, 0, F_WATER, S_WATER, L_WATER, H_STONE, FXM_WATER, NULL},
	/* 11 */ {{0, 100, 50, 250}, 0, F_WATER, S_WATER, L_WATER, H_STONE, FXM_SPLAT, "green"},
	/* 12 */ {{190, 100, 40, 250}, 0, F_WATER, S_WATER, L_WATER, H_STONE, FXM_SPLAT, "orange"},
	/* 13 */ {{180, 140, 20, 250}, 0, F_WATER, S_WATER, L_WATER, H_STONE, FXM_SPLAT, "brown"},
	/* 14 */ {{180, 140, 60, 250}, 1, F_WOOD, S_WOOD, L_WOOD, H_WOOD, FXM_CONCRETE, NULL},
	/* 15 */ {{120, 90, 30, 250}, 1, F_WOOD, S_WOOD, L_WOOD, H_WOOD, FXM_CONCRETE, NULL},
	/* 16 */ {{80, 80, 80, 250}, 1, F_WOOD, S_WOOD, L_WOOD, H_WOOD, FXM_CONCRETE, NULL},
	/* 17 */ STONE_ROW(180, 180, 180),
	/* 18 */ STONE_ROW(90, 90, 90),
	/* 19 */ STONE_ROW(180, 140, 50),
	/* 20 */ STONE_ROW(130, 90, 30),
	/* 21 */ STONE_ROW(240, 240, 240),
	/* 22 */ STONE_ROW(80, 140, 80),
	/* 23 */ STONE_ROW(120, 20, 20),
	/* 24 */ STONE_ROW(10, 10, 10),
	/* 25 */ {{20, 120, 20, 250}, 0, F_GRASS, S_GRASS, L_GRASS, H_STONE, FXM_CONCRETE, NULL},
	/* 26 */ {{120, 120, 20, 250}, 0, F_GRASS, S_GRASS, L_GRASS, H_STONE, FXM_CONCRETE, NULL},
	/* 27 */ {{100, 25, 25, 250}, 0, F_WATER, S_WATER, L_WATER, H_STONE, FXM_SPLAT, "red"},
	/* 28 */ METAL_ROW(FXM_METAL, H_METAL, NULL),
	/* 29 */ METAL_ROW(FXM_PIPE, H_METAL, "environ/waterspurt"),
	/* 30 */ METAL_ROW(FXM_PIPE, H_METAL, "environ/oilspurt"),
	/* 31 */ METAL_ROW(FXM_PIPE, H_METAL, "environ/chemspurt"),
	/* 32 */ METAL_ROW(FXM_COMPUTER, H_METAL, NULL),
	/* 33 */ {{180, 140, 50, 250}, 2, F_STONE, S_STONE, L_STONE, H_STONE, FXM_DUST, NULL},
	/* 34 */ {{180, 180, 180, 250}, 2, F_STONE, S_STONE, L_STONE, H_STONE, FXM_DUST, NULL},
	/* 35 */ {{180, 180, 180, 250}, 0, F_FLESH, S_STONE, L_STONE, H_STONE, FXM_FLESH, NULL},
	/* 36 */ {{23, 25, 25, 250}, 0, F_WATER, S_WATER, L_WATER, H_STONE, FXM_SPLAT, "black"},
	/* 37 */ METAL_ROW(FXM_GLASS, H_GLASS, NULL),
	/* 38 */ METAL_ROW(FXM_LITPANEL, H_GLASS, NULL),
	/* 39 */ METAL_ROW(FXM_VENDING, H_GLASS, NULL),
	/* 40 */ {{240, 240, 240, 250}, 1, F_WOOD, S_WOOD, L_WOOD, H_WOOD, FXM_PAPER, NULL},
	/* 41 */ {{23, 25, 25, 250}, 1, F_METAL, S_METAL, L_METAL, H_STONE, FXM_PAPERBURST, NULL},
	/* 42 */ METAL_ROW(FXM_VENT, H_METAL, NULL),
};

#define CHUNKS "objects/generic/"

const fx_debris_model_t fx_debris_models[FX_NUM_DEBRIS_MODELS] = {
	/* 0 */ {CHUNKS "chunks_brick", "BRICK", 3},
	/* 1 */ {CHUNKS "chunks_glass", "GLASS_SM", 3},
	/* 2 */ {CHUNKS "chunks_glass", "GLASS_MED", 3},
	/* 3 */ {CHUNKS "chunks_glass", "GLASS_LRG", 3},
	/* 4 */ {CHUNKS "chunks_metal", "METAL_SM", 4},
	/* 5 */ {CHUNKS "chunks_metal", "METAL_MED", 4},
	/* 6 */ {CHUNKS "chunks_metal", "METAL_LG", 4},
	/* 7 */ {CHUNKS "chunks_rock", "ROCK_SM", 4},
	/* 8 */ {CHUNKS "chunks_rock", "ROCK_MED", 4},
	/* 9 */ {CHUNKS "chunks_rock", "ROCK_LG", 4},
	/* 10 */ {CHUNKS "chunks_rock", "ROCK_TINY", 4},
	/* 11 */ {CHUNKS "chunks_wall", "WALL_SM", 4},
	/* 12 */ {CHUNKS "chunks_wall", "WALL_MED", 4},
	/* 13 */ {CHUNKS "chunks_wall", "WALL_LRG", 4},
	/* 14 */ {CHUNKS "chunks_wall", "WALL_TINY", 4},
	/* 15 */ {CHUNKS "chunks_wood", "WOOD_SM", 3},
	/* 16 */ {CHUNKS "chunks_wood", "WOOD_MED", 3},
	/* 17 */ {CHUNKS "chunks_wood", "WOOD_LARGE", 3},
	/* 18 */ {"items/projectiles", "SHELL", 1},
	/* 19 */ {"effects/explosion", "EXPLODE80", 1},
	/* 20 */ {"effects/explosion", "EXPLODE20", 1},
	/* 21 */ {"objects/generic/soda_can", "CAN", 1},
	/* 22 */ {"objects/generic/paper_blowing", "PAPER", 1},
	/* 23 */ {"effects/explosion", "EXPLODE80", 1}, /* mushroom: no own file found */
};

int
FX_DebrisParts(int model)
{
	if ((model < 0) || (model >= FX_NUM_DEBRIS_MODELS))
	{
		return 1;
	}

	return fx_debris_models[model].parts;
}
