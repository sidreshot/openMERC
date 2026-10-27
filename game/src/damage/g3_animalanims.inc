// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - gameplay attributes of the dog and cow animations (row layout as in g3_humananims.inc).
// The dog rows are addressed by position from g3_animals.cpp (Rec(i) there); row 5 is a placeholder kept so
// the positions stay stable.
// Implements: SPEC-722 (Dogs, Cows)
#define G3_DOG_ANIMS \
	G3A("std_ibark_n", STATIONARY, 0.0f, 10.0f, -10.0f, 0, STAND, 0.0f, 0) \
	G3A("std_ipant_n", STATIONARY, 0.0f, 10.0f, -10.0f, 0, STAND, 0.0f, 0) \
	G3A("std_r_n", MOVE, 1.1f, 20.0f, -20.0f, ANIMF_MATCH_PHASE | ANIMF_LOOP, STAND, 0.0f, 0) \
	G3A("std_stoside_rt", DEATH, 1.0f, 0.0f, 0.0f, 0, ONSIDE, 0.0f, 0) \
	G3A("std_w_n", MOVE, 1.0f, 20.0f, -20.0f, ANIMF_MATCH_PHASE | ANIMF_LOOP, STAND, 0.0f, 0) \
	G3A("unused", MOVE, 1.0f, 20.0f, -20.0f, ANIMF_MATCH_PHASE | ANIMF_LOOP, STAND, 0.0f, 0) \
	G3A("std_jattack_n", ATTACK, 1.0f, 20.0f, -20.0f, 0, STAND, 0.0f, 0) \
	G3A("std_wsniff_n", MOVE, 1.0f, 20.0f, -20.0f, ANIMF_MATCH_PHASE | ANIMF_LOOP, STAND, 0.0f, 0) \
	G3A("std_jattack_n", ATTACK, 1.0f, 20.0f, -20.0f, 0, STAND, 0.4f, 0) \
	G3A("std_stumble_rt", DEATH, 1.0f, 0.0f, 0.0f, 0, FACEDOWN, 0.0f, 0) \
	G3A("sit_i_n", STATIONARY, 0.0f, 10.0f, -10.0f, 0, STAND, 0.0f, 0) \
	G3A("sit_itostand_n", STATIONARY, 1.0f, 10.0f, -10.0f, 0, STAND, 0.0f, 0) \
	G3A("std_itosit_n", STATIONARY, 1.0f, 10.0f, -10.0f, 0, STAND, 0.0f, 0) \
	G3A("std_wsniff_frm", MOVE, 1.0f, 20.0f, -20.0f, 0, STAND, 0.0f, 0) \
	G3A("std_wsniff_to", MOVE, 1.0f, 20.0f, -20.0f, 0, STAND, 0.0f, 0) \
	G3A("std_wlooklt_frm", MOVE, 1.0f, 20.0f, -20.0f, 0, STAND, 0.0f, 0) \
	G3A("std_wlooklt_n", MOVE, 1.0f, 20.0f, -20.0f, ANIMF_MATCH_PHASE | ANIMF_LOOP, STAND, 0.0f, 0) \
	G3A("std_wlooklt_to", MOVE, 1.0f, 20.0f, -20.0f, 0, STAND, 0.0f, 0) \
	G3A("std_wlookltrt_n", MOVE, 1.0f, 20.0f, -20.0f, ANIMF_MATCH_PHASE, STAND, 0.0f, 0) \
	G3A("std_wlookrt_frm", MOVE, 1.0f, 20.0f, -20.0f, 0, STAND, 0.0f, 0) \
	G3A("std_wlookrt_n", MOVE, 1.0f, 20.0f, -20.0f, ANIMF_MATCH_PHASE | ANIMF_LOOP, STAND, 0.0f, 0) \
	G3A("std_wlookrt_to", MOVE, 1.0f, 20.0f, -20.0f, 0, STAND, 0.0f, 0) \
	G3A("sid_prdl_rt", DEATH, 0.0f, 0.0f, 0.0f, ANIMF_REHITTABLE, ONSIDE, 0.0f, 0) \
	G3A("sid_prdl2_rt", DEATH, 0.0f, 0.0f, 0.0f, ANIMF_REHITTABLE | ANIMF_LOOP, ONSIDE, 0.0f, 0) \
	G3A("sid_prdl3_rt", DEATH, 0.0f, 0.0f, 0.0f, ANIMF_REHITTABLE | ANIMF_LOOP, ONSIDE, 0.0f, 0) \
	G3A("std_sblown_2bk", DEATH, 1.0f, 0.0f, 0.0f, 0, ONSIDE, 0.0f, 0)

#define G3_COW_ANIMS \
	G3A("std_i_n", STATIONARY, 0.0f, 0.0f, 0.0f, ANIMF_LOOP, STAND, 0.0f, 0) \
	G3A("std_w_n", MOVE, 1.0f, 10.0f, -10.0f, ANIMF_LOOP, STAND, 0.0f, 0) \
	G3A("std_s_rt", DEATH, 1.0f, 0.0f, 0.0f, ANIMF_FINISH_CYCLE, ONSIDE, 0.0f, 0) \
	G3A("std_imoo_n", STATIONARY, 1.0f, 0.0f, 0.0f, ANIMF_FINISH_CYCLE, STAND, 0.0f, 0) \
	G3A("std_ilooking_n", STATIONARY, 1.0f, 0.0f, 0.0f, ANIMF_FINISH_CYCLE, STAND, 0.0f, 0) \
	G3A("std_p_n", STATIONARY, 1.0f, 0.0f, 0.0f, ANIMF_FINISH_CYCLE, STAND, 0.0f, 0)
