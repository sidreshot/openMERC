// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: per-classname data of the civilian/officer ("ecto") and female families.
// Each type has its skin registration list (SPEC-730 §4: [level] marks, default 3) and its set-up function
// (SPEC-730 §5 notation: weapons, accessories, skins and parts, face prefix, voice, armour).
//
// Implements: SPEC-730 §7 (ecto roster), §8 (female roster)
#include "rg_local.h"

#define S(m, s, l) {RGR_SKIN, m, s, l}
#define F(p, l) {RGR_FACE, NULL, p, l}
#define T(f) {RGR_TALK, NULL, f, 3}
#define E {RGR_END, NULL, NULL, 0}

static const char *HB = "abolt_head_t", *HT = "to_abolt_head_t";
static const char *HIPL = "abolt_hip_l", *HIPR = "abolt_hip_r";

// ---------------------------------------------------------------------------------------------------------------
// §7 civilian/officer family (enemy/ecto); skins include c=c_black unless noted
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_eskinhead3[] = {S("b", "b_nycskinhleader", 3), S("a", "a_nycskinhleader", 3),
                                       S("b", "b_nycskinhleader2", 1), S("a", "a_nycskinhleader2", 1),
                                       S("h", "h_w_12_s", 3), F("f_w_12_", 3), S("h", "h_w_1_s", 1), F("f_w_1_", 1),
                                       E};
static void su_eskinhead3(RgSetup &s)
{
	if (!s.unarmed()) {
		s.right(3);
		s.racc(0.25f, "acc_holster", "acc_holster_blck", HIPR, "to_abolt_hip_r", 1.125f);
	}
	s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_sun", HB, HT, 1.0f);
	s.racc(0.5f, "acc_hat_baseball", "acc_hat_baseball_grcamo", HB, "to_abolt_head_t_bkwd", 1.125f);
	if (!(s.half() && s.skin("b", "b_nycskinhleader2") && s.skin("a", "a_nycskinhleader2"))) {
		s.skin("b", "b_nycskinhleader");
		s.skin("a", "a_nycskinhleader");
	}
	if (!(s.half() && s.head("h_w_1_s", "f_w_1_")))
		s.head("h_w_12_s", "f_w_12_");
	s.voice("sk", 1, 0);
	s.effect("environ/onfireburst");
}

static const RgReg reg_estockbroker[] = {S("b", "b_nycmalestockbroker", 3), S("a", "a_nycmalestockbroker", 3),
                                         S("a", "a_nycmalestockbrokerb", 1), S("h", "h_b_1_s", 1), F("f_b_1_", 1),
                                         S("h", "h_w_2_c", 3), F("f_w_2_", 3), E};
static void su_estockbroker(RgSetup &s)
{
	s.racc(0.5f, "w_briefcase", "rubbish", "wbolt_hand_l", "to_wbolt_hand_l", 1.0f);
	s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_norm", HB, HT, 1.0f);
	s.skin("c", "c_black");
	s.skin("b", "b_nycmalestockbroker");
	if (s.set("stockbroke2") && s.has("a", "a_nycmalestockbrokerb") && s.has("h", "h_b_1_s")) {
		s.skin("a", "a_nycmalestockbrokerb");
		s.head("h_b_1_s", "f_b_1_");
	} else {
		if (s.odd())
			s.off("_lhairbang");
		if (s.odd())
			s.off("_rhairbang");
		s.skin("a", "a_nycmalestockbroker");
		s.head("h_w_2_c", "f_w_2_");
	}
	s.voice("npc", 1, 0);
}

static const RgReg reg_etourist[] = {S("b", "b_dcmaletourist", 3), S("a", "a_dcmaletourist", 3),
                                     S("h", "h_w_4_s", 3), F("f_w_4_", 3), E};
static void su_etourist(RgSetup &s)
{
	s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_norm", HB, HT, 1.0f);
	s.skin("c", "c_black");
	s.skin("b", "b_dcmaletourist");
	s.skin("a", "a_dcmaletourist");
	s.head("h_w_4_s", "f_w_4_");
	s.voice("npc", 1, 0);
}

static const RgReg reg_ebum[] = {S("b", "b_nycbum", 3), S("a", "a_nycbum", 3), S("hb", "hb_h_1_b", 3),
                                 F("f_hb_1_", 3), E};
static void su_ebum(RgSetup &s)
{
	s.acc("acc_hat_flop", "acc_hat_flop_brwn", HB, HT, 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_nycbum");
	s.skin("a", "a_nycbum");
	s.headmat("hb", "hb_h_1_b", "f_hb_1_");
	s.voice("npc", 1, 0);
}

// types 19, 20, 27 have set-ups but no classname (SPEC-730 open question 4); their set names are not given
static const RgReg reg_t19[] = {S("b", "b_colmaleprisoner1", 3), S("a", "a_colmaleprisoner1", 3),
                                S("hb", "hb_h_1_b", 3), F("f_hb_1_", 3), E};
static void su_t19(RgSetup &s)
{
	s.acc("acc_hat_flop", "acc_hat_flop_brwn", HB, HT, 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_colmaleprisoner1");
	s.skin("a", "a_colmaleprisoner1");
	s.headmat("hb", "hb_h_1_b", "f_hb_1_");
	s.voice("npc", 1, 0);
}

static const RgReg reg_t20[] = {S("b", "b_colmaleprisoner2", 3), S("a", "a_colmaleprisoner2", 3),
                                S("h", "h_h_1_c", 3), F("f_h_1_", 3), E};
static void su_t20(RgSetup &s)
{
	s.skin("c", "c_black");
	s.skin("b", "b_colmaleprisoner2");
	s.skin("a", "a_colmaleprisoner2");
	s.head("h_h_1_c", "f_h_1_");
	s.voice("npc", 1, 0);
}

static const RgReg reg_eman1[] = {S("b", "b_irqmalecitizen", 3), S("a", "a_irqmalecitizen", 3), S("h", "h_m_2_s", 3),
                                  F("f_m_2_", 3), E};
static void su_eman1(RgSetup &s)
{
	s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_norm", HB, HT, 1.0f);
	if (!s.racc(0.333f, "acc_hat_turbin", "acc_hat_turbin_wht", HB, HT, 1.125f))
		s.racc(0.5f, "acc_hat_top", "acc_hat_top_wht", HB, HT, 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_irqmalecitizen");
	s.skin("a", "a_irqmalecitizen");
	s.skin("h", "h_m_2_s");
	s.off("_headbeard");
	s.face("f_m_2_");
	s.voice("npc", 1, 0);
}

static const RgReg reg_ecommander[] = {S("b", "b_ugncommander", 3), S("a", "a_ugncommander", 3),
                                       S("h", "h_b_2_s", 3), F("f_b_2_", 3), S("h", "h_b_1_s", 0), F("f_b_1_", 0), E};
static void su_ecommander(RgSetup &s)
{
	s.right(4);
	s.acc("acc_hat_general", "acc_hat_general_brwn", HB, HT, 1.125f);
	s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_sun", HB, HT, 1.0f);
	s.racc(0.5f, "acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", 1.0f);
	s.racc(0.5f, "acc_pouch_thigh", "acc_pouch_thigh_tan", "abolt_thigh_l", "to_abolt_thigh_l", 1.125f);
	s.racc(0.5f, "acc_box_hip_smll", "acc_box_hip_smll_brwn", HIPL, "to_abolt_hip_l", 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_ugncommander");
	s.skin("a", "a_ugncommander");
	if (!(s.odd() && s.head("h_b_1_s", "f_b_1_")))
		s.head("h_b_2_s", "f_b_2_");
	s.voice("so", 1, 0);
}

static const RgReg reg_kos_eofficer[] = {S("b", "b_kosofficer", 3), S("a", "a_kosofficer", 3), S("h", "h_w_5_c", 1),
                                         F("f_w_5_", 1), S("h", "h_w_2_c", 0), F("f_w_2_", 0), S("h", "h_h_6_s", 3),
                                         F("f_h_6_", 3), E};
static void su_kos_eofficer(RgSetup &s)
{
	s.right(4);
	s.acc("acc_hat_general2", "acc_hat_general2_grn", HB, HT, 1.125f);
	s.racc(0.25f, "acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_kosofficer");
	s.skin("a", "a_kosofficer");
	if (s.pick(3) == 0 && s.head("h_w_2_c", "f_w_2_"))
		;
	else if (s.half() && s.head("h_w_5_c", "f_w_5_"))
		;
	else
		s.head("h_h_6_s", "f_h_6_");
	s.voice("so", 1, 0);
}

static const RgReg reg_erefugee[] = {S("b", "b_kosrefugee", 3), S("a", "a_kosrefugee", 3), S("h", "h_w_4_s", 3),
                                     F("f_w_4_", 3), E};
static void su_erefugee(RgSetup &s)
{
	s.right(4);
	s.racc(0.2f, "acc_hat_baseball", "acc_hat_baseball_tan", HB, "to_abolt_head_t_fwd", 1.125f);
	s.racc(0.25f, "acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_kosrefugee");
	s.skin("a", "a_kosrefugee");
	s.skin("h", "h_w_4_s");
	s.off("_headbeard");
	s.face("f_w_4_");
	s.voice("npc", 1, 0);
	s.effect("environ/onfireburst");
}

static const RgReg reg_irq_eofficer[] = {S("b", "b_irqofficer", 3), S("a", "a_irqofficer", 3), S("h", "h_m_1_s", 3),
                                         F("f_m_1_", 3), E};
static void su_irq_eofficer(RgSetup &s)
{
	s.right(4);
	s.acc("acc_hat_beret", "acc_hat_beret_blck", HB, HT, 1.125f);
	s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_sun", HB, HT, 1.0f);
	s.racc(0.5f, "acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", 1.125f);
	s.racc(0.5f, "acc_pouch_thigh", "acc_pouch_thigh_blck", "abolt_thigh_l", "to_abolt_thigh_l", 1.125f);
	s.racc(0.5f, "acc_box_hip_smll", "acc_box_hip_smll_brwn", HIPL, "to_abolt_hip_l", 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_irqofficer");
	s.skin("a", "a_irqofficer");
	s.head("h_m_1_s", "f_m_1_");
	s.voice("so", 1, 0);
}

static const RgReg reg_eworker[] = {S("b", "b_ugnfactoryworker", 3), S("a", "a_ugnfactoryworker", 3),
                                    S("h", "h_b_4_c", 1), F("f_b_4_", 1), S("h", "h_b_3_s", 3), F("f_b_3_", 3), E};
static void su_eworker(RgSetup &s)
{
	s.right(4);
	s.acc("acc_hat_top", "acc_hat_top_wht", HB, HT, 1.125f);
	s.racc(0.5f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPL, "to_abolt_hip_l", 1.125f);
	s.racc(0.5f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPR, "to_abolt_hip_r", 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_ugnfactoryworker");
	s.skin("a", "a_ugnfactoryworker");
	if (!(s.half() && s.head("h_b_4_c", "f_b_4_")))
		s.head("h_b_3_s", "f_b_3_");
	s.off("_headcrew");
	s.voice("so", 1, 0);
}

static const RgReg reg_t27[] = {S("b", "b_colchemist", 3), S("a", "a_colchemist", 3), S("h", "h_h_1_c", 3),
                                F("f_h_1_", 3), S("h", "h_h_2_s", 3), F("f_h_2_", 3), E};
static void su_t27(RgSetup &s)
{
	s.right(4);
	if (!s.racc(0.333f, "acc_ecto_glasses", "acc_glasses_norm", HB, HT, 1.0f))
		s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_sun", HB, HT, 1.0f);
	s.racc(0.5f, "acc_hat_top", "acc_hat_top_wht", HB, HT, 1.125f);
	s.racc(0.5f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPL, "to_abolt_hip_l", 1.125f);
	s.racc(0.5f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPR, "to_abolt_hip_r", 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_colchemist");
	s.skin("a", "a_colchemist");
	if (!(s.set("chemist2") && s.head("h_h_1_c", "f_h_1_")))
		s.head("h_h_2_s", "f_h_2_");
	s.voice("soldier", 2, 0);
}

static const RgReg reg_sib_eofficer[] = {S("b", "b_sibsuit", 3), S("a", "a_sibsuit", 3), F("f_w_5_", 3),
                                         S("h", "h_w_5_c", 3), F("f_w_3_", 2), S("h", "h_w_3_s", 2), E};
static void su_sib_eofficer(RgSetup &s)
{
	s.right(3);
	s.racc(0.5f, "w_briefcase", "rubbish", "wbolt_hand_l", "to_wbolt_hand_l", 1.0f);
	s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_norm", HB, HT, 1.0f);
	bool hat = s.acc("acc_hat_tall", "acc_hat_tall_gry", HB, HT, 1.125f);
	s.racc(0.5f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPL, "to_abolt_hip_l", 1.125f);
	s.racc(0.5f, "acc_box_holster", "acc_holster_blck", HIPR, "to_abolt_hip_r", 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_sibsuit");
	s.skin("a", "a_sibsuit");
	if (s.set("sibsuit2") && s.head("h_w_3_s", "f_w_3_"))
		s.off("_headcrew");
	else {
		s.head("h_w_5_c", "f_w_5_");
		s.off(hat ? "_headcrew" : "_headbald");
	}
	s.voice("so", 1, 0);
}

static const RgReg reg_escientist1[] = {S("b", "b_sibscientist", 3), S("a", "a_sibscientist", 3), F("f_w_2_", 3),
                                        S("h", "h_w_2_c", 3), F("f_w_1_", 0), S("h", "h_w_1_s", 0), E};
static void su_escientist1(RgSetup &s)
{
	s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_norm", HB, HT, 1.0f);
	s.racc(0.5f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPL, "to_abolt_hip_l", 1.125f);
	s.racc(0.5f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPR, "to_abolt_hip_r", 1.125f);
	s.skin("c", "c_black");
	s.skin("b", "b_sibscientist");
	s.skin("a", "a_sibscientist");
	if (!(s.set("sibscience2") && s.head("h_w_1_s", "f_w_1_")))
		s.head("h_w_2_c", "f_w_2_");
	s.voice("so", 1, 0);
}

static const RgReg reg_epunk[] = {F("f_w_12_", 3), S("b", "b_nycpunk2", 3), S("a", "a_nycpunk2", 3),
                                  S("h", "h_w_12_s", 3), E};
static void su_epunk(RgSetup &s)
{
	s.right(3, true);
	s.racc(0.5f, "acc_ecto_glasses", "acc_glasses_sun", HB, HT, 1.0f);
	s.skin("c", "c_black");
	s.skin("a", "a_nycpunk2");
	s.skin("b", "b_nycpunk2");
	s.head("h_w_12_s", "f_w_12_");
	s.voice("sk", 1, 0);
}

#define ECTO(cn, t, set, set2, ss, reg, su) {cn, t, RG_FAM_ECTO, set, set2, ss, 1.0f, false, reg, su}
const RgHumanType rg_ecto_types[] = {
	ECTO("m_nyc_eskinhead3", 15, "skinleader", NULL, RG_SET_B, reg_eskinhead3, su_eskinhead3),
	ECTO("m_nyc_estockbroker", 16, "stockbroke", "stockbroke2", RG_SET_A, reg_estockbroker, su_estockbroker),
	ECTO("m_nyc_etourist", 17, "tourist", NULL, RG_SET_A, reg_etourist, su_etourist),
	ECTO("m_nyc_ebum", 18, "bum", NULL, RG_SET_A, reg_ebum, su_ebum),
	ECTO("", 19, "bum", NULL, RG_SET_A, reg_t19, su_t19),         // no classname; set name UNVERIFIED
	ECTO("", 20, "bum", NULL, RG_SET_A, reg_t20, su_t20),         // no classname; set name UNVERIFIED
	ECTO("m_irq_eman1", 21, "irqcitizen", NULL, RG_SET_A, reg_eman1, su_eman1),
	ECTO("m_afr_ecommander", 22, "commander", NULL, RG_SET_C, reg_ecommander, su_ecommander),
	ECTO("m_kos_eofficer", 23, "serbofficer", NULL, RG_SET_C, reg_kos_eofficer, su_kos_eofficer),
	ECTO("m_kos_erefugee", 24, "kosrefugee", NULL, RG_SET_C, reg_erefugee, su_erefugee),
	ECTO("m_irq_eofficer", 25, "iraqofficer", NULL, RG_SET_C, reg_irq_eofficer, su_irq_eofficer),
	ECTO("m_afr_eworker", 26, "factory", NULL, RG_SET_B, reg_eworker, su_eworker),
	ECTO("", 27, "chemist", "chemist2", RG_SET_B, reg_t27, su_t27), // no classname; set names UNVERIFIED
	ECTO("m_sib_eofficer", 28, "sibsuit1", "sibsuit2", RG_SET_C, reg_sib_eofficer, su_sib_eofficer),
	ECTO("m_sib_escientist1", 29, "sibscience1", "sibscience2", RG_SET_C, reg_escientist1, su_escientist1),
	ECTO("m_nyc_epunk", 30, "zitpunk", NULL, RG_SET_B, reg_epunk, su_epunk),
	{NULL, 0, 0, NULL, NULL, 0, 0, false, NULL, NULL}};

// ---------------------------------------------------------------------------------------------------------------
// §8 female family (enemy/female); death-voice type 5 for all
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_fskinchick[] = {S("b", "b_nycskinchick", 3), S("a", "a_nycskinchick", 3),
                                       S("h", "h_w_15_c", 3), F("f_w_15_", 3), E};
static void su_fskinchick(RgSetup &s)
{
	s.right(3);
	s.skin("b", "b_nycskinchick");
	s.skin("a", "a_nycskinchick");
	s.head("h_w_15_c", "f_w_15_");
	s.voice("skf", 1, 5);
}

static const RgReg reg_fwoman[] = {S("b", "b_nycwomanw", 3), S("a", "a_nycwomanw", 3), S("h", "h_w_18_m", 3),
                                   F("f_w_18_", 3), S("b", "b_nycwomanb", 1), S("a", "a_nycwomanb", 1),
                                   S("h", "h_b_8_c", 1), F("f_b_8_", 1), E};
static void su_fwoman(RgSetup &s)
{
	if (s.half() && s.has("b", "b_nycwomanb") && s.has("a", "a_nycwomanb") && s.has("h", "h_b_8_c")) {
		s.skin("b", "b_nycwomanb");
		s.skin("a", "a_nycwomanb");
		s.head("h_b_8_c", "f_b_8_");
		s.off("_medmhairmhead");
	} else {
		s.skin("b", "b_nycwomanw");
		s.skin("a", "a_nycwomanw");
		s.head("h_w_18_m", "f_w_18_");
		s.off("_crewhair");
		s.off("_lear");
		s.off("_rear");
	}
	s.voice("npcf", 1, 5);
}

static const RgReg reg_fguard2[] = {S("b", "b_sibguard2", 3), S("a", "a_sibguard2", 3), S("h", "h_w_18_m", 1),
                                    F("f_w_18_", 1), S("h", "h_w_17_c", 3), F("f_w_17_", 3), E};
static void su_fguard2(RgSetup &s)
{
	s.right(5);
	s.racc(0.5f, "acc_hat_baseball", "acc_hat_baseball_sib", HB, HT, 1.125f);
	s.skin("b", "b_sibguard2");
	s.skin("a", "a_sibguard2");
	if (s.half() && s.head("h_w_18_m", "f_w_18_")) {
		s.off("_crewhair");
		s.off("_lear");
		s.off("_rear");
	} else {
		s.head("h_w_17_c", "f_w_17_");
		s.off("_medmhairmhead");
	}
	s.voice("", 1, 5);
	s.armour(1);
}

static const RgReg reg_fscientist2[] = {S("b", "b_sibscientist2", 3), S("a", "a_sibscientist2", 3),
                                        S("h", "h_w_18_m", 1), F("f_w_18_", 1), S("h", "h_w_17_c", 3),
                                        F("f_w_17_", 3), E};
static void su_fscientist2(RgSetup &s)
{
	s.skin("b", "b_sibscientist2");
	s.skin("a", "a_sibscientist2");
	if (s.pick(3) < 2 && s.head("h_w_18_m", "f_w_18_")) {
		if (s.half()) {
			s.off("_crewhair");
			s.off("_lear");
			s.off("_rear");
		} else {
			s.off("_medmhairmhead");
			s.off("_lbang");
			s.off("_rbang");
		}
	} else {
		s.head("h_w_17_c", "f_w_17_");
		s.off("_medmhairmhead");
		s.off("_lbang");
		s.off("_rbang");
	}
	s.voice("", 1, 5);
}

static const RgReg reg_irqwoman1[] = {S("b", "b_irqwoman1", 3), S("a", "a_irqwoman1", 3), S("h", "h_irqwoman1", 3),
                                      S("f", "f_irqwoman1", 3), E};
static void su_irqwoman1(RgSetup &s)
{
	s.skin("b", "b_irqwoman1");
	s.skin("a", "a_irqwoman1");
	s.skin("h", "h_irqwoman1");
	s.skin("f", "f_irqwoman1");
	s.voice("npcf", 1, 5);
}
static const RgReg reg_irqwoman2[] = {S("b", "b_irqwoman2", 3), S("a", "a_irqwoman2", 3), S("h", "h_irqwoman2", 3),
                                      S("f", "f_irqwoman2", 3), E};
static void su_irqwoman2(RgSetup &s)
{
	s.skin("b", "b_irqwoman2");
	s.skin("a", "a_irqwoman2");
	s.skin("h", "h_irqwoman2");
	s.skin("f", "f_irqwoman2");
	s.voice("npcf", 1, 5);
}

static void rg_tokhead(RgSetup &s)
{
	if (s.half() && s.head("h_j_8_m", "f_j_8_")) {
		s.off("_crewhair");
		s.off("_lear");
		s.off("_rear");
	} else {
		s.head("h_j_7_c", "f_j_7_");
		s.off("_medmhairmhead");
	}
}

static const RgReg reg_tokwoman1[] = {S("b", "b_tokwoman1", 3), S("a", "a_tokwoman1", 3), S("h", "h_j_8_m", 1),
                                      F("f_j_8_", 1), S("h", "h_j_7_c", 3), F("f_j_7_", 3), E};
static void su_tokwoman1(RgSetup &s)
{
	s.racc(0.5f, "acc_glasses", "acc_glasses_norm", HB, HT, 1.0f);
	s.skin("b", "b_tokwoman1");
	s.skin("a", "a_tokwoman1");
	rg_tokhead(s);
	s.voice("npcf", 1, 5);
}
static const RgReg reg_tokwoman2[] = {S("b", "b_tokwoman2", 3), S("a", "a_tokwoman2", 3), S("h", "h_j_8_m", 1),
                                      F("f_j_8_", 1), S("h", "h_j_7_c", 3), F("f_j_7_", 3), E};
static void su_tokwoman2(RgSetup &s)
{
	s.racc(0.5f, "acc_glasses", "acc_glasses_norm", HB, HT, 1.0f);
	s.skin("b", "b_tokwoman2");
	s.skin("a", "a_tokwoman2");
	rg_tokhead(s);
	s.voice("npcf", 1, 5);
}

static const RgReg reg_fassassin[] = {S("b", "b_tokassassin", 3), S("a", "a_tokassassin", 3), S("h", "h_j_8_m", 1),
                                      F("f_j_8_", 1), S("h", "h_j_7_c", 3), F("f_j_7_", 3), E};
static void su_fassassin(RgSetup &s)
{
	s.right(3);
	s.left(30);
	s.skin("b", "b_tokassassin");
	s.skin("a", "a_tokassassin");
	rg_tokhead(s);
	s.voice("skinhead", 2, 5);
}

static const RgReg reg_ffemale[] = {S("b", "b_dcfemale", 3), S("a", "a_dcfemale", 3), S("h", "h_raider5", 3),
                                    S("f", "f_raider5", 3), E};
static void su_ffemale(RgSetup &s)
{
	s.left(26);
	s.right(18);
	s.acc("acc_tanks", "acc_tanks_cas", "abolt_back", "to_abolt_back", 1.125f);
	s.skin("b", "b_dcfemale");
	s.skin("a", "a_dcfemale");
	s.skin("h", "h_raider5");
	s.skin("f", "f_raider5");
	s.voice("raf", 1, 5);
	s.armour(7);
}

static const RgReg reg_ftaylor[] = {S("b", "b_xtaylor", 3), S("a", "a_xtaylor", 3), S("h", "h_taylor", 3),
                                    F("f_taylor_", 3), T("f_taylor_n"), E};
static void su_ftaylor(RgSetup &s)
{
	if (s.ai)
		s.ai->buddy = 1;
	s.right(6, true);
	s.skin("b", "b_xtaylor");
	s.skin("a", "a_xtaylor");
	s.head("h_taylor", "f_taylor_");
	s.voice("taylor", 1, 5);
	s.armour(7);
}

#define FEM(cn, t, set, ss, reg, su) {cn, t, RG_FAM_FEMALE, set, NULL, ss, 1.0f, false, reg, su}
const RgHumanType rg_female_types[] = {
	FEM("m_nyc_fskinchick", 100, "skinchick", RG_SET_B, reg_fskinchick, su_fskinchick),
	FEM("m_nyc_fwoman", 101, "nywoman", RG_SET_A, reg_fwoman, su_fwoman),
	{"m_sib_fguard2", 102, RG_FAM_FEMALE, "sibguard", NULL, RG_SET_B, 1.0f, true, reg_fguard2, su_fguard2},
	FEM("m_sib_fscientist2", 103, "sibscience", RG_SET_A, reg_fscientist2, su_fscientist2),
	FEM("m_irq_fwoman1", 104, "irqwoman1", RG_SET_A, reg_irqwoman1, su_irqwoman1),
	FEM("m_irq_fwoman2", 105, "irqwoman2", RG_SET_A, reg_irqwoman2, su_irqwoman2),
	FEM("m_tok_fwoman1", 106, "tokwoman1", RG_SET_A, reg_tokwoman1, su_tokwoman1),
	FEM("m_tok_fwoman2", 107, "tokwoman2", RG_SET_A, reg_tokwoman2, su_tokwoman2),
	FEM("m_tok_fassassin", 108, "tokassassin", RG_SET_B, reg_fassassin, su_fassassin),
	{"m_cas_ffemale", 109, RG_FAM_FEMALE, "raider", NULL, RG_SET_B, 1.0f, true, reg_ffemale, su_ffemale},
	{"m_x_ftaylor", 110, RG_FAM_FEMALE, "taylor", NULL, RG_SET_B, 1.0f, false, reg_ftaylor, su_ftaylor},
	{NULL, 0, 0, NULL, NULL, 0, 0, false, NULL, NULL}};
