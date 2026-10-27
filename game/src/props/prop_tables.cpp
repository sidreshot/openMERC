// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — prop model tables (SPEC-770 §2, Appendix A).
// Model directory and file names are those of the props in the game data (ghoul/objects/...); health, solidity,
// materials and debris are gameplay parameters matched to the props' observed behaviour.
// Entry order and indices are part of the savegame format (sequence reference = table * 65536 + index).
// Table 4 (items) belongs to SPEC-781, tables 10-12 to the vehicle characters (SPEC-731); they are not listed here.
// Implements: SPEC-770 §2, Appendix A

#include "props.h"

namespace props {

static const ModelEntry t_bosnia[] = {
	{"objects/bosnia/antipersonnel_jack", "jack", 15, 18, 500, 2, NULL, 0, 0.0f}, // 0 (data no)
	{"objects/bosnia/flag_wall", "flag_wall", 15, 18, 500, 2, NULL, 0, 0.0f}, // 1 (data no)
	{"objects/bosnia/landmine", "landmine", 28, 12, 1, 2, NULL, 0, 0.0f}, // 2 (data no)
	{"objects/bosnia/rubble1", "rubble1", 17, 15, 1000, 2, NULL, 0, 0.0f}, // 3 (data no)
	{"objects/bosnia/rubble2", "rubble2", 0, 16, 1000, 2, NULL, 0, 0.0f}, // 4 (data no)
	{"objects/bosnia/searchbeam", "null", 0, 0, 1000, 0, NULL, 0, 0.0f}, // 5 (data no)
	{"objects/bosnia/searchbeam", "beam", 0, 0, 1000, 0, NULL, 0, 0.0f}, // 6
	{"objects/bosnia/searchlight", "base", 28, 9, 1000, 2, NULL, 0, 0.0f}, // 7
	{"objects/bosnia/searchlight", "arm", 28, 9, 1000, 2, NULL, 0, 0.0f}, // 8
	{"objects/bosnia/searchlight", "lamp", 28, 9, 1000, 2, "glass", 0, 0.0f}, // 9
	{"objects/bosnia/tank_jack", "jack", 28, 9, 500, 2, NULL, 0, 0.0f}, // 10 (data no)
	{"objects/bosnia/truck_chunks", "chunks", 28, 9, 500, 2, "chunks", 0, 0.0f}, // 11 (data no)
	{"objects/bosnia/truck_old", "truck", 28, 9, 1000, 2, "truck", 0, 0.0f}, // 12
	{"objects/bosnia/truck_old", "tire", 28, 9, 1000, 2, NULL, 0, 0.0f}, // 13
};
static const ModelEntry t_castle[] = {
	{"objects/castle/armor", "armor", 28, 9, 500, 2, NULL, 0, 1.0f}, // 0
	{"objects/castle/bust", "bust", 18, 16, 1000, 2, "bust", 0, 1.0f}, // 1
	{"objects/castle/cannon", "cannon", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 2
	{"objects/castle/chair", "seat01", 15, 18, 500, 2, NULL, 0, 1.0f}, // 3
	{"objects/castle/chair", "leg01", 15, 18, 500, 2, NULL, 0, 1.0f}, // 4
	{"objects/castle/chair", "leg02", 15, 18, 500, 2, NULL, 0, 1.0f}, // 5
	{"objects/castle/chair", "back", 15, 18, 500, 2, NULL, 0, 1.0f}, // 6
	{"objects/castle/chair_big", "seat01", 15, 18, 500, 2, NULL, 0, 1.0f}, // 7
	{"objects/castle/chair_big", "leg01", 15, 18, 500, 2, NULL, 0, 1.0f}, // 8
	{"objects/castle/chair_big", "arm01", 15, 18, 500, 2, NULL, 0, 1.0f}, // 9
	{"objects/castle/chair_big", "back01", 15, 18, 500, 2, NULL, 0, 1.0f}, // 10
	{"objects/castle/globe", "globe", 0, 25, 1000, 2, NULL, 0, 1.0f}, // 11
	{"objects/castle/globe", "base", 0, 25, 1000, 2, NULL, 0, 1.0f}, // 12
	{"objects/castle/hedge", "hedge", 15, 18, 500, 2, NULL, 25, 1.0f}, // 13
	{"objects/castle/tree", "tree", 15, 18, 1000, 2, NULL, 35, 2.0f}, // 14
};
static const ModelEntry t_generic[] = {
	{"objects/generic/access_card", "card", 0, 0, 500, 2, NULL, 0, 1.0f}, // 0 (data no)
	{"objects/generic/banner_generic", "banner", 0, 0, 0, 2, "banner", 0, 1.0f}, // 1
	{"objects/generic/barbwire_coil", "barbwire", 28, 0, 500, 2, NULL, 15, 1.0f}, // 2
	{"objects/generic/barrel_burning", "barrel", 28, 9, 260, 2, NULL, 0, 2.0f}, // 3
	{"objects/generic/barrel_chemical", "barrel", 28, 9, 260, 2, "barrel", 0, 2.0f}, // 4
	{"objects/generic/barrel_wine", "barrel", 14, 17, 500, 2, NULL, 0, 2.0f}, // 5 (data no)
	{"objects/generic/bench", "bottom", 14, 17, 250, 2, "bench", 10, 2.0f}, // 6
	{"objects/generic/bench", "top", 14, 17, 250, 2, "bench", 10, 2.0f}, // 7
	{"objects/generic/blinds", "blinds", 28, 9, 200, 2, "blinds", 0, 2.0f}, // 8 (data no)
	{"objects/generic/bookcase", "case", 14, 17, 500, 2, "case", 0, 3.0f}, // 9
	{"objects/generic/bookshelf", "shelf", 14, 17, 500, 2, "shelf", 0, 1.0f}, // 10
	{"objects/generic/bookshelf", "middle", 14, 17, 500, 2, "shelf", 0, 1.0f}, // 11 (data no)
	{"objects/generic/broom", "broom", 14, 17, 60, 2, "broom", 0, 1.0f}, // 12
	{"objects/generic/bunk", "side2", 28, 8, 500, 2, "bunk", 0, 2.0f}, // 13
	{"objects/generic/bunk", "bed", 28, 8, 200, 0, "bunk", 0, 2.0f}, // 14
	{"objects/generic/bunk", "side1", 28, 8, 200, 0, "bunk", 0, 2.0f}, // 15
	{"objects/generic/car_american", "car", 28, 8, 2000, 2, "car", 0, 1.0f}, // 16
	{"objects/generic/car_european", "car", 28, 8, 2000, 2, "car", 0, 1.0f}, // 17
	{"objects/generic/car_honda", "car", 28, 8, 2000, 2, "car", 0, 1.0f}, // 18
	{"objects/generic/car_rolls", "car", 28, 8, 2000, 2, NULL, 0, 1.0f}, // 19 (data no)
	{"objects/generic/car_sports", "car", 28, 8, 2000, 2, "car", 0, 1.0f}, // 20
	{"objects/generic/cart", "cart", 28, 8, 1000, 2, "cart", 0, 1.0f}, // 21
	{"objects/generic/cash_register", "register", 28, 8, 500, 2, "register", 0, 1.0f}, // 22
	{"objects/generic/chair_legs", "chair", 28, 8, 400, 2, "legs", 0, 1.0f}, // 23
	{"objects/generic/chair_legs", "legs", 28, 8, 400, 2, "legs", 0, 1.0f}, // 24
	{"objects/generic/chair_legs2", "legs2", 28, 8, 400, 2, NULL, 0, 1.0f}, // 25 (data no)
	{"objects/generic/chair_office", "top", 28, 11, 600, 2, "office", 0, 1.0f}, // 26
	{"objects/generic/chair_office", "base", 28, 11, 600, 2, "office", 0, 1.0f}, // 27
	{"objects/generic/chunks_brick", "brick", 20, 2, 50, 2, "brick", 0, 1.0f}, // 28
	{"objects/generic/chunks_glass", "glass", 37, 7, 25, 2, NULL, 0, 1.0f}, // 29 (data no)
	{"objects/generic/chunks_metal", "metal", 28, 8, 100, 2, "metal", 0, 1.0f}, // 30 (data no)
	{"objects/generic/chunks_rock", "rock", 20, 14, 50, 2, "rock", 0, 1.0f}, // 31 (data no)
	{"objects/generic/chunks_wall", "wall", 20, 14, 10, 2, "wall", 0, 1.0f}, // 32 (data no)
	{"objects/generic/chunks_wood", "wood", 15, 18, 30, 2, "wood", 0, 1.0f}, // 33 (data no)
	{"objects/generic/comp_keyboard", "keyboard", 28, 25, 100, 2, NULL, 0, 1.0f}, // 34
	{"objects/generic/comp_monitor", "monitor", 38, 25, 250, 2, "monitor", 0, 1.0f}, // 35
	{"objects/generic/comp_monitor2", "monitor2", 38, 25, 250, 2, "monitor2", 0, 1.0f}, // 36
	{"objects/generic/comp_monitor2", "tower", 38, 25, 250, 2, "monitor2", 0, 1.0f}, // 37
	{"objects/generic/crate_fish", "crate", 14, 17, 500, 2, NULL, 0, 1.0f}, // 38
	{"objects/generic/desk_phone", "phone", 28, 25, 50, 2, "phone", 0, 1.0f}, // 39
	{"objects/generic/fan", "fan", 28, 11, 500, 2, NULL, 0, 1.0f}, // 40
	{"objects/generic/faucet", "faucet", 28, 11, 150, 2, NULL, 0, 1.0f}, // 41
	{"objects/generic/fire_extinguisher", "exting", 28, 9, 150, 2, "exting", 0, 1.0f}, // 42
	{"objects/generic/fire_hydrant", "hydrant", 28, 9, 500, 2, NULL, 0, 1.0f}, // 43
	{"objects/generic/fish", "fish", 35, 32, 150, 2, "fish", 0, 1.0f}, // 44
	{"objects/generic/fish", "fish", 35, 32, 150, 2, "fish", 0, 1.0f}, // 45
	{"objects/generic/flag_pole", "flag_pole", 28, 9, 500, 2, "flag_pole", 0, 1.0f}, // 46
	{"objects/generic/forklift", "forklift", 28, 9, 2000, 2, "forklift", 0, 1.0f}, // 47
	{"objects/generic/garbage_bag", "bag", 0, 25, 100, 2, NULL, 0, 1.0f}, // 48
	{"objects/generic/gun_auto", "gun", 28, 9, 2000, 2, "gun", 0, 1.0f}, // 49
	{"objects/generic/gun_auto", "base", 28, 9, 2000, 2, "gun", 0, 1.0f}, // 50
	{"objects/generic/gun_auto", "arm", 28, 9, 2000, 2, "gun", 0, 1.0f}, // 51
	{"objects/generic/gun_auto", "null", 28, 9, 2000, 2, NULL, 0, 1.0f}, // 52
	{"objects/generic/gun_big", "base", 28, 9, 1, 2, "gun", 0, 2.0f}, // 53
	{"objects/generic/gun_big", "gunleft", 28, 9, 1, 2, "gun", 0, 2.0f}, // 54
	{"objects/generic/gun_big", "gunright", 28, 9, 1, 2, "gun", 0, 2.0f}, // 55
	{"objects/generic/gun_big", "shield", 28, 9, 1, 2, "gun", 0, 2.0f}, // 56
	{"objects/generic/heap_generic", "heap", 0, 0, 200, 2, "heap", 0, 1.0f}, // 57
	{"objects/generic/beam", "beam", -1, -1, 250, 0, NULL, 0, 1.0f}, // 58
	{"objects/light/flare", "flare", -1, -1, 500, 0, NULL, 0, 1.0f}, // 59
	{"objects/generic/motorcycle", "motorcycle", 28, 9, 2000, 2, "motorcycle", 0, 1.0f}, // 60 (data no)
	{"objects/generic/orange_cone", "orangecone", 21, 31, 500, 2, "orangecone", 5, 2.0f}, // 61
	{"objects/generic/palette_jack", "pallette_jack", 28, 11, 1000, 2, NULL, 15, 1.0f}, // 62
	{"objects/generic/paper_blowing", "paper", 0, 0, 25, 2, NULL, 0, 1.0f}, // 63
	{"objects/generic/parking_meter", "parking_meter", 28, 11, 500, 2, NULL, 0, 1.0f}, // 64 (data no)
	{"objects/generic/phone_booth", "phone_booth", 28, 11, 1000, 2, NULL, 0, 1.0f}, // 65
	{"objects/generic/pillow", "pillow", 21, 31, 100, 2, "pillow", 4, 1.0f}, // 66
	{"objects/generic/plant_potted", "plant", 0, 11, 200, 2, "plant", 0, 1.0f}, // 67
	{"objects/generic/plant_tall", "plant", 0, 0, 200, 2, NULL, 0, 1.0f}, // 68
	{"objects/generic/radar", "radar_base", 28, 9, 5000, 2, "radar_base", 0, 1.0f}, // 69
	{"objects/generic/radar", "radar_dish", 28, 9, 5000, 2, "radar_dish", 0, 1.0f}, // 70
	{"objects/generic/radio", "radio", 28, 9, 100, 2, NULL, 0, 1.0f}, // 71
	{"objects/generic/security_camera/test", "camera", 28, 9, 200, 2, "camera", 10, 1.0f}, // 72
	{"objects/generic/security_camera/test", "arm", 28, 9, 200, 2, "camera", 10, 1.0f}, // 73
	{"objects/generic/shovel", "shovel", 28, 9, 200, 2, "shovel", 0, 1.0f}, // 74
	{"objects/generic/shrub1", "shrub", 0, 0, 200, 2, "shrub", 0, 1.0f}, // 75
	{"objects/generic/sink", "sink", 17, 15, 200, 2, "sink", 0, 1.0f}, // 76
	{"objects/generic/sofa", "sofa", 0, 0, 2000, 2, "sofa", 0, 1.0f}, // 77
	{"objects/generic/sofa_chair", "chair", 0, 0, 1000, 2, "chair", 0, 1.0f}, // 78
	{"objects/generic/speaker", "speaker", 28, 9, 100, 2, NULL, 0, 1.0f}, // 79
	{"objects/generic/stoplight", "stoplight", 28, 9, 2000, 2, "stoplight", 0, 1.0f}, // 80 (data no)
	{"objects/generic/street_name_sign", "sign", 28, 9, 500, 2, "sign", 0, 1.0f}, // 81 (data no)
	{"objects/generic/sword", "sword", 28, 9, 150, 2, NULL, 0, 1.0f}, // 82
	{"objects/generic/sword", "sword", 28, 9, 150, 2, NULL, 0, 1.0f}, // 83
	{"objects/generic/table_round", "table", 28, 9, 400, 2, "table", 0, 1.0f}, // 84
	{"objects/generic/table1", "top", 28, 9, 400, 2, "table1", 0, 1.0f}, // 85
	{"objects/generic/table1", "back", 28, 9, 400, 2, "table1", 0, 1.0f}, // 86
	{"objects/generic/table1", "leg", 28, 9, 400, 2, "table1", 0, 1.0f}, // 87
	{"objects/generic/table2", "top01", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 88
	{"objects/generic/table2", "side01", 28, 9, 1000, 0, NULL, 0, 1.0f}, // 89
	{"objects/generic/table2", "side02", 28, 9, 1000, 0, NULL, 0, 1.0f}, // 90
	{"objects/generic/table2", "side03", 28, 9, 1000, 0, NULL, 0, 1.0f}, // 91
	{"objects/generic/table2", "top02", 28, 9, 1000, 0, NULL, 0, 1.0f}, // 92
	{"objects/generic/tank1", "tank", 28, 9, 4000, 2, NULL, 0, 1.0f}, // 93 (data no)
	{"objects/generic/tank_gas", "gas", 28, 9, 8, 2, NULL, 0, 3.0f}, // 94
	{"objects/generic/tank_propane", "propane", 28, 9, 8, 2, NULL, 0, 1.0f}, // 95
	{"objects/generic/toilet", "toilet", 21, 31, 200, 2, "toilet", 0, 2.0f}, // 96
	{"objects/generic/train_generic", "train", 28, 9, 9000, 2, "train", 0, 1.0f}, // 97
	{"objects/generic/trash_ashtray", "ashtray", 28, 9, 100, 2, NULL, 0, 1.0f}, // 98
	{"objects/generic/trashcan_barrel", "trashcan", 28, 9, 200, 2, NULL, 0, 1.0f}, // 99
	{"objects/generic/tree_dead", "tree", 15, 18, 1000, 2, NULL, 0, 1.0f}, // 100 (data no)
	{"objects/generic/tree_thin", "tree", 15, 18, 500, 2, NULL, 0, 1.0f}, // 101 (data no)
	{"objects/generic/truckcab", "truckcab", 28, 9, 3000, 2, NULL, 0, 1.0f}, // 102
	{"objects/generic/tv_ceiling", "tv", 28, 9, 300, 2, "face", 10, 1.0f}, // 103
	{"objects/generic/tv_ceiling", "supports", 28, 9, 300, 2, NULL, 10, 1.0f}, // 104
	{"objects/generic/tv_wall", "tv_wall", 28, 9, 300, 2, "tv_wall", 10, 1.0f}, // 105
	{"objects/generic/urinal", "urinal", 21, 31, 200, 2, "urinal", 0, 1.0f}, // 106
	{"objects/generic/urinal_cake", "urinal_cake", 21, 15, 100, 2, NULL, 0, 4.0f}, // 107
	{"objects/generic/urn", "urn", 37, 23, 25, 2, NULL, 20, 1.0f}, // 108
	{"objects/generic/waste_basket", "waste_basket", 28, 9, 150, 2, "waste_basket", 0, 1.0f}, // 109
	{"objects/generic/web_moss_animated", "web_moss", 0, 0, 150, 0, "web_moss", 0, 1.0f}, // 110
	{"objects/generic/wet_floor_sign", "sign", 0, 0, 150, 2, NULL, 0, 1.0f}, // 111
	{"Enemy/Bolt", "w_bat", 15, 18, 400, 2, NULL, 0, 1.0f}, // 112 (data no)
	{"Enemy/Bolt", "w_desert_eagle", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 113 (data no)
	{"Enemy/Bolt", "w_pistol1", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 114
	{"Enemy/Bolt", "w_machinepistol", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 115
	{"Enemy/Bolt", "w_katana", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 116 (data no)
	{"Enemy/Bolt", "w_machinegun", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 117
	{"Enemy/Bolt", "w_sniperrifle", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 118
	{"Enemy/Bolt", "w_shotgun", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 119
	{"Items/Projectiles", "c4", 28, 9, 1000, 2, NULL, 0, 1.0f}, // 120
};
static const ModelEntry t_iraq[] = {
	{"objects/iraq/basket", "basket", 15, 18, 50, 2, NULL, 0, 0.0f}, // 0
	{"objects/iraq/cart_camel", "cart", 15, 18, 300, 2, NULL, 0, 0.0f}, // 1
	{"objects/iraq/flatbed", "off", 28, 8, 500, 2, NULL, 0, 0.0f}, // 2
	{"objects/iraq/flatbed", "tire", 28, 8, 500, 2, NULL, 0, 0.0f}, // 3
	{"objects/iraq/flatbed", "on", 28, 8, 500, 2, NULL, 0, 0.0f}, // 4
	{"objects/iraq/fruit_melon", "melon", 27, 0, 50, 2, NULL, 0, 0.0f}, // 5
	{"objects/iraq/fruit_orange", "orange", 12, 0, 50, 2, NULL, 0, 0.0f}, // 6
	{"objects/iraq/fruit_stand", "fruit_stand", 15, 18, 125, 2, NULL, 0, 0.0f}, // 7
	{"objects/generic/beam", "beam", -1, -1, 0, 0, NULL, 0, 0.0f}, // 8
	{"objects/iraq/partition", "partition", -1, 0, 400, 2, NULL, 0, 0.0f}, // 9
	{"objects/iraq/pottery", "pottery", 4, 0, 25, 2, NULL, 0, 0.0f}, // 10
	{"objects/iraq/sprinklers", "sprinklers", 28, 8, 500, 2, NULL, 0, 0.0f}, // 11 (data no)
	{"objects/iraq/stealth", "stealth", 28, 8, 5000, 2, NULL, 0, 0.0f}, // 12
	{"objects/iraq/tree_palm", "tree", 15, 18, 1000, 2, NULL, 0, 0.0f}, // 13
	{"objects/iraq/valve", "valve", 28, 8, 300, 2, NULL, 0, 0.0f}, // 14
};
static const ModelEntry t_lights[] = {
	{"objects/light/alley_wall", "light", 28, 10, 500, 0, NULL, 0, 0.0f}, // 0
	{"objects/light/alley_wall_old", "light", 28, 10, 500, 2, NULL, 0, 0.0f}, // 1 (data no)
	{"objects/light/caged", "caged", 28, 10, 500, 2, NULL, 0, 0.0f}, // 2
	{"objects/light/candle", "candle", 28, 10, 500, 2, NULL, 0, 0.0f}, // 3
	{"objects/light/candle", "candle2", 28, 10, 500, 2, NULL, 0, 0.0f}, // 4
	{"objects/light/candle", "candle3", 28, 10, 500, 2, NULL, 0, 0.0f}, // 5
	{"objects/light/candle", "base", 28, 10, 500, 2, "candle", 0, 0.0f}, // 6
	{"objects/light/candle2", "base", 28, 10, 500, 2, "candle", 0, 0.0f}, // 7
	{"objects/light/chandelier", "swing", 28, 10, 500, 2, "light", 0, 0.0f}, // 8
	{"objects/light/desk_lamp", "lamp", 28, 10, 500, 2, "light", 0, 0.0f}, // 9
	{"objects/light/emergency", "emergency", 28, 10, 500, 2, NULL, 0, 0.0f}, // 10
	{"objects/light/emergency_blue", "emergency", 28, 10, 500, 2, NULL, 0, 0.0f}, // 11
	{"objects/light/halfshell", "shell", 28, 10, 500, 2, NULL, 0, 0.0f}, // 12
	{"objects/light/hanging", "hanging", 28, 10, 500, 2, NULL, 0, 0.0f}, // 13
	{"objects/light/lantern", "lantern", 28, 31, 500, 2, NULL, 8, 1.0f}, // 14
	{"objects/light/lantern2", "lantern2", 28, 10, 500, 2, NULL, 0, 0.0f}, // 15
	{"objects/light/lantern3", "lantern3", 28, 10, 500, 2, NULL, 0, 0.0f}, // 16
	{"objects/light/military_door", "light", 28, 10, 500, 2, NULL, 0, 0.0f}, // 17 (data no)
	{"objects/light/military_door_outside", "light", 28, 10, 500, 2, NULL, 0, 0.0f}, // 18
	{"objects/light/shop", "shop", 28, 10, 500, 2, "light", 0, 0.0f}, // 19
	{"objects/light/street_modern", "modern", 28, 10, 500, 0, NULL, 0, 0.0f}, // 20
	{"objects/light/street_old", "old", 28, 10, 500, 2, NULL, 0, 0.0f}, // 21
	{"objects/light/lamp_hang", "lamp", 28, 10, 500, 2, NULL, 0, 0.0f}, // 22
};
static const ModelEntry t_newyork[] = {
	{"objects/newyork/burned_car", "burned_car", 28, 10, 10000, 2, NULL, 0, 0.0f}, // 0
	{"objects/newyork/gumball", "gumball", 28, 11, 1000, 2, "gumball", 0, 0.0f}, // 1
	{"objects/newyork/news_machine", "news", 28, 11, 1000, 2, NULL, 0, 0.0f}, // 2 (data no)
	{"objects/newyork/trash_pile", "trash_pile", 28, 10, 300, 2, NULL, 0, 0.0f}, // 3 (data no)
	{"objects/newyork/trashcan", "trashcan", 28, 10, 300, 2, NULL, 0, 0.0f}, // 4
};
static const ModelEntry t_siberia[] = {
	{"objects/siberia/carton1", "carton1", 28, 9, 500, 2, NULL, 0, 0.0f}, // 0 (data no)
	{"objects/siberia/carton2", "carton2", 28, 9, 10000, 2, NULL, 0, 0.0f}, // 1 (data no)
	{"objects/siberia/carton3", "carton3", 28, 9, 10000, 2, NULL, 0, 0.0f}, // 2
	{"objects/siberia/carton4", "carton4", 28, 9, 10000, 2, NULL, 0, 0.0f}, // 3 (data no)
	{"objects/siberia/flag", "flag", 0, 0, 500, 2, NULL, 0, 0.0f}, // 4
	{"objects/siberia/icicles", "icicles", 0, 0, 10000, 2, NULL, 0, 0.0f}, // 5 (data no)
	{"objects/generic/beam", "beam", -1, -1, 250, 0, NULL, 0, 0.0f}, // 6
	{"objects/siberia/flare", "null", -1, -1, 500, 0, NULL, 0, 0.0f}, // 7
	{"objects/siberia/searchlight", "lamp", 28, 9, 10000, 2, NULL, 0, 0.0f}, // 8
	{"objects/siberia/searchlight", "arm", 28, 9, 10000, 2, NULL, 0, 0.0f}, // 9
	{"objects/siberia/snowcat", "snowcat", 28, 9, 10000, 2, NULL, 0, 0.0f}, // 10
};
static const ModelEntry t_tokyo[] = {
	{"objects/tokyo/bowl", "bowl", 14, 17, 100, 2, NULL, 6, 1.0f}, // 0
	{"objects/tokyo/buddha", "buddha", 28, 11, 2000, 2, NULL, 0, 0.0f}, // 1 (data no)
	{"objects/tokyo/fan", "fan", 14, 14, 50, 2, NULL, 0, 4.0f}, // 2
	{"objects/tokyo/gong", "gong", 28, 11, 1000, 2, NULL, 6, 0.5f}, // 3
	{"objects/tokyo/gym_equip1", "equip1", 28, 11, 2000, 2, NULL, 0, 0.0f}, // 4 (data no)
	{"objects/tokyo/gym_equip2", "equip2", 28, 11, 2000, 2, NULL, 0, 0.0f}, // 5 (data no)
	{"objects/tokyo/plant_bonsai", "bonsai", 14, 17, 100, 2, NULL, 0, 0.0f}, // 6
	{"objects/tokyo/samurai_suit_small", "samurai_sm", 28, 10, 1000, 2, NULL, 0, 2.0f}, // 7
	{"objects/tokyo/samurai_suit_small", "samurai_sm", 28, 10, 1000, 2, NULL, 0, 0.0f}, // 8
	{"objects/tokyo/shoe", "shoe", 14, 17, 100, 2, NULL, 4, 1.0f}, // 9
	{"objects/tokyo/stone_light", "light", 17, 15, 1000, 2, NULL, 0, 0.0f}, // 10
	{"objects/generic/stunt_chopper", "stunt_chopper", 28, 11, 1, 2, NULL, 0, 0.0f}, // 11 (data no)
};
static const ModelEntry t_uganda[] = {
	{"objects/uganda/african_tree", "tree", 15, 18, 1000, 2, NULL, 0, 0.0f}, // 0 (data no)
	{"objects/uganda/chainsaw", "chainsaw", 28, 9, 500, 2, NULL, 0, 0.0f}, // 1
	{"objects/uganda/cow", "cow", 27, 0, 400, 2, NULL, 0, 0.0f}, // 2 (data no)
	{"objects/uganda/cow_head", "head", 27, 0, 100, 2, NULL, 0, 0.0f}, // 3
	{"objects/uganda/gore_pile", "gore_pile", 27, 0, 300, 2, NULL, 0, 0.0f}, // 4
	{"objects/uganda/gutted_cow", "gutted_cow", 27, 32, 1000, 2, NULL, 0, 0.0f}, // 5
	{"objects/uganda/hook", "hook", 28, 9, 1000, 2, NULL, 0, 0.0f}, // 6
	{"objects/uganda/nuke", "nuke", 28, 9, 1000, 2, NULL, 0, 0.0f}, // 7
	{"objects/uganda/rocket_core", "core", 28, 9, 500, 2, NULL, 0, 0.0f}, // 8
	{"objects/uganda/rocket_engine", "engine", 28, 9, 500, 2, NULL, 0, 0.0f}, // 9
	{"objects/uganda/side_o_beef", "beef", 27, 32, 500, 2, "beef", 0, 0.0f}, // 10
	{"objects/uganda/train_arm", "arm", 28, 9, 500, 2, NULL, 0, 0.0f}, // 11 (data no)
	{"objects/uganda/train_bumper", "bumper", 28, 9, 500, 2, NULL, 0, 0.0f}, // 12
	{"objects/uganda/train_coupling", "coupling", 28, 9, 500, 2, NULL, 0, 0.0f}, // 13
	{"objects/uganda/train_wheel", "wheel", 28, 9, 500, 2, NULL, 0, 0.0f}, // 14 (data no)
};

struct TableDesc {
	const ModelEntry *e;
	int n;
};
#define T(x) {x, (int)(sizeof(x) / sizeof(x[0]))}
static const TableDesc s_tables[13] = {T(t_bosnia), T(t_castle), T(t_generic), T(t_iraq), {NULL, 41}, T(t_lights),
                                       T(t_newyork), T(t_siberia), T(t_tokyo), T(t_uganda), {NULL, 12}, {NULL, 5},
                                       {NULL, 3}};
#undef T

static_assert(sizeof(t_bosnia) / sizeof(t_bosnia[0]) == 14, "table 0: 14 entries");
static_assert(sizeof(t_castle) / sizeof(t_castle[0]) == 15, "table 1: 15 entries");
static_assert(sizeof(t_generic) / sizeof(t_generic[0]) == 121, "table 2: 121 entries");
static_assert(sizeof(t_iraq) / sizeof(t_iraq[0]) == 15, "table 3: 15 entries");
static_assert(sizeof(t_lights) / sizeof(t_lights[0]) == 23, "table 5: 23 entries");
static_assert(sizeof(t_newyork) / sizeof(t_newyork[0]) == 5, "table 6: 5 entries");
static_assert(sizeof(t_siberia) / sizeof(t_siberia[0]) == 11, "table 7: 11 entries");
static_assert(sizeof(t_tokyo) / sizeof(t_tokyo[0]) == 12, "table 8: 12 entries");
static_assert(sizeof(t_uganda) / sizeof(t_uganda[0]) == 15, "table 9: 15 entries");

const ModelEntry *Entry(int table, int index)
{
	if (table < 0 || table >= 13 || !s_tables[table].e || index < 0 || index >= s_tables[table].n)
		return NULL;
	return &s_tables[table].e[index];
}

gg::ModelRef Ref(const ModelEntry *e)
{
	gg::ModelRef r = {e ? e->dir : "", e ? e->file : "", e ? e->skinmat : NULL};
	return r;
}

} // namespace props
