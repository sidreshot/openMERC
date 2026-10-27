// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: per-classname data of the male soldier family ("meso") and of the three
// human bosses (registration lists and set-up functions, SPEC-730 §5 notation).
//
// Implements: SPEC-730 §9 (meso roster), SPEC-732 §4 (bosses)
#include "rg_local.h"

#define S(m, s, l) {RGR_SKIN, m, s, l}
#define F(p, l) {RGR_FACE, NULL, p, l}
#define T(f) {RGR_TALK, NULL, f, 3}
#define E {RGR_END, NULL, NULL, 0}

static const char *HB = "abolt_head_t", *HT = "to_abolt_head_t";
static const char *HIPL = "abolt_hip_l", *HIPR = "abolt_hip_r";
static const char *THL = "abolt_thigh_l", *THR = "abolt_thigh_r";
static const char *BK = "abolt_back";
static const float M = 8.0f / 7.0f; // meso accessory scale 1.142857 (bit-exact as the original sends it)

static void ba(RgSetup &s, const char *base)
{
	s.skin("b", va("b_%s", base));
	s.skin("a", va("a_%s", base));
}
static bool ba2(RgSetup &s, const char *base)
{
	char b[64], a[64];
	Com_sprintf(b, sizeof(b), "b_%s", base);
	Com_sprintf(a, sizeof(a), "a_%s", base);
	if (!s.has("b", b) || !s.has("a", a))
		return false;
	s.skin("b", b);
	s.skin("a", a);
	return true;
}
static void offs(RgSetup &s, const char *a, const char *b = NULL, const char *c = NULL)
{
	s.off(a);
	if (b)
		s.off(b);
	if (c)
		s.off(c);
}

// ---------------------------------------------------------------------------------------------------------------
// named characters and allies
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_john[] = {F("f_john_", 3), S("b", "b_xjohn", 3), S("a", "a_xjohn", 3), S("h", "h_john", 3),
                                 T("f_john_n"), T("f_john_m"), E};
static void su_john(RgSetup &s)
{
	s.acc("acc_backpack_smll", "acc_backpack_smll_dgrey", BK, "to_abolt_back", M);
	s.acc("acc_hat_flop3", "acc_hat_flop3", HB, HT, 1.05f);
	s.acc("acc_headset", "acc_headset", HB, HT, M);
	s.acc("acc_box_hip", "acc_box_hip_canteen1", HIPL, "to_abolt_hip_l", M);
	s.acc("acc_holster", "acc_holster_blck", "abolt_calf_r", "to_abolt_hip_r", M);
	s.right(3, true);
	ba(s, "xjohn");
	s.head("h_john", "f_john_");
	s.voice("jm", 1, 0);
}

static const RgReg reg_johnsnow[] = {F("f_john_", 3), S("b", "b_xjohn_snow", 3), S("a", "a_xjohn_snow", 3),
                                     S("h", "h_john", 3), T("f_john_n"), T("f_john_m"), E};
static void su_johnsnow(RgSetup &s)
{
	s.acc("acc_backpack_lrg", "acc_backpack_lrg_blk", BK, "to_abolt_back", M);
	s.acc("acc_hat_snow", "acc_hat_snow", HB, HT, M);
	s.acc("acc_headset", "acc_headset", HB, HT, M);
	s.right(3, true);
	ba(s, "xjohn_snow");
	s.head("h_john", "f_john_");
	s.voice("jm", 1, 0);
}

static const RgReg reg_johndesert[] = {F("f_john_", 3), S("b", "b_xjohn_desert", 3), S("a", "a_xjohn_desert", 3),
                                       S("h", "h_john", 3), T("f_john_n"), T("f_john_m"), E};
static void su_johndesert(RgSetup &s)
{
	s.acc("acc_backpack_smll", "acc_backpack_smll", BK, "to_abolt_back", M);
	s.acc("acc_hat_flop3", "acc_hat_flop3_desert", HB, HT, 1.05f);
	s.acc("acc_headset", "acc_headset", HB, HT, M);
	s.acc("acc_box_hip", "acc_box_hip_canteen1", HIPL, "to_abolt_hip_l", M);
	s.right(3, true);
	ba(s, "xjohn_desert");
	s.head("h_john", "f_john_");
	s.voice("jm", 1, 0);
}

static const RgReg reg_hawk[] = {F("f_b_6_", 3), S("b", "b_mercdemo1", 3), S("a", "a_mercdemo1", 3),
                                 S("h", "h_b_6_s", 3), T("f_b_6_n"), T("f_b_6_f"), E};
static void su_mmerc(RgSetup &s)
{
	s.ent->flags |= FL_NO_KNOCKBACK;
	if (s.ai)
		s.ai->buddy = 1;
	s.acc("acc_backpack_lrg", "acc_backpack_lrg_tan", BK, "to_abolt_back", M);
	s.acc("acc_glasses", "acc_glasses_sun", HB, HT, M);
	s.acc("acc_headset", "acc_headset", HB, HT, M);
	s.right(5, true);
	ba(s, "mercdemo1");
	s.head("h_b_6_s", "f_b_6_");
	s.voice("hawk", 1, 0);
	s.sound("impact/Hawk/LookOut.adp");
}
static void su_mhurtmerc(RgSetup &s)
{
	ba(s, "mercdemo1");
	s.head("h_b_6_s", "f_b_6_");
	s.voice("hawk", 1, 0);
}

static const RgReg reg_sam[] = {F("f_sam_", 3), S("b", "b_xsam", 3), S("a", "a_xsam", 3), S("h", "h_sam", 3),
                                T("f_sam_n"), E};
static void su_msam(RgSetup &s)
{
	if (s.ai)
		s.ai->buddy = 1;
	s.acc("acc_glasses", "acc_glasses_norm", HB, HT, M);
	ba(s, "xsam");
	s.head("h_sam", "f_sam_");
	s.voice("sam", 1, 0);
}

static const RgReg reg_swatlead[] = {S("b", "b_nycswat", 3), S("a", "a_nycswat", 3), S("h", "h_b_4_c", 3),
                                     F("f_b_4_", 3), T("f_b_4_n"), E};
static void su_mswatleader(RgSetup &s)
{
	if (s.ai)
		s.ai->buddy = 1;
	s.right(3);
	s.acc("acc_helmet2", "acc_helmet2", HB, HT, M);
	s.acc("acc_goggles", "acc_goggles", HB, HT, M);
	s.acc("acc_holster", "acc_holster_blck", HIPR, "to_abolt_hip_r", M);
	s.acc("acc_box_hip_smll", "acc_box_hip_smll_blck", HIPL, "to_abolt_hip_l", M);
	ba(s, "nycswat");
	s.head("h_b_4_c", "f_b_4_");
	s.voice("sw", 1, 0);
	s.armour(1);
}

static const RgReg reg_swat[] = {S("b", "b_nycswat", 3), S("a", "a_nycswat2", 3), S("h", "h_w_3_s", 3),
                                 F("f_w_3_", 3), E};
static void su_mswat(RgSetup &s)
{
	s.right(3);
	s.acc("acc_helmet2", "acc_helmet2", HB, HT, M);
	s.acc("acc_goggles", "acc_goggles", HB, HT, M);
	s.racc(0.25f, "acc_holster", "acc_holster_blck", HIPR, "to_abolt_hip_r", M);
	s.racc(0.25f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPL, "to_abolt_hip_l", M);
	s.skin("b", "b_nycswat");
	s.skin("a", "a_nycswat2");
	s.head("h_w_3_s", "f_w_3_");
	s.voice("sw", 1, 0);
	s.armour(1);
}

static const RgReg reg_kosrebel[] = {S("b", "b_kosrebel1", 2), S("a", "a_kosrebel1", 2), S("b", "b_kosrebel2", 3),
                                     S("a", "a_kosrebel2", 3), S("h", "h_w_19_s", 3), F("f_w_19_", 3), E};
static void su_mrebel(RgSetup &s)
{
	s.right(6);
	s.racc(0.25f, "acc_backpack_low", "acc_backpack_low_dgrey", BK, "to_abolt_back", M);
	s.racc(0.25f, "acc_box_hip", "acc_box_hip_canteen1", HIPL, "to_abolt_hip_l", M);
	s.skin("c", "c_black");
	if (!(s.half() && ba2(s, "kosrebel1")))
		ba(s, "kosrebel2");
	s.head("h_w_19_s", "f_w_19_");
	if (s.racc(0.25f, "acc_helmet", "acc_helmet_green1", HB, HT, M))
		offs(s, "_crewhead", "_baldhead", "_comface");
	else {
		offs(s, "_helmethead", "_helmetface");
		s.racc(0.25f, "acc_hat_baseball", "acc_hat_baseball_grcamo", HB, "to_abolt_head_t_fwd", M);
		s.off("_crewhead");
	}
	s.voice("npc", 1, 0);
	s.effect("environ/onfireburst");
}

static const RgReg reg_klaleader[] = {S("b", "b_kosrebel2", 3), S("a", "a_kosrebel2", 3), S("h", "h_w_20_c", 3),
                                      F("f_w_20_", 3), T("f_w_20_n"), E};
static void su_mklaleader(RgSetup &s)
{
	if (s.ai)
		s.ai->buddy = 1;
	s.right(6);
	s.acc("acc_box_hip", "acc_box_hip_canteen1", HIPL, "to_abolt_hip_l", M);
	s.acc("acc_hat_baseball", "acc_hat_baseball_grcamo", HB, "to_abolt_head_t_fwd", M);
	s.skin("c", "c_black");
	ba(s, "kosrebel2");
	s.head("h_w_20_c", "f_w_20_");
	offs(s, "_helmethead", "_helmetface", "_crewhead");
	s.voice("", 1, 0);
	s.effect("environ/onfireburst");
}

static const RgReg reg_politician[] = {S("b", "b_dcmalepol", 3), S("a", "a_dcmalepol", 3), S("h", "h_b_2_s", 3),
                                       F("f_b_2_", 3), E};
static void su_mpolitician(RgSetup &s)
{
	s.racc(0.5f, "w_briefcase", "rubbish", "wbolt_hand_l", "to_wbolt_hand_l", 1.0f);
	s.racc(0.25f, "acc_glasses", "acc_glasses_norm", HB, HT, M);
	ba(s, "dcmalepol");
	s.head("h_b_2_s", "f_b_2_");
	s.voice("npc", 1, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// New York gang (team 3)
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_nypunk[] = {S("b", "b_nycpunk1", 3), S("a", "a_nycpunk1", 3), S("h", "h_w_3_s", 3),
                                   F("f_w_3_", 3), E};
static void su_mpunk(RgSetup &s)
{
	s.right(11);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	s.acc("acc_spikes_shlder", "acc_spikes_shlder", "abolt_shoulder_r", "to_abolt_shoulder_r", M);
	s.acc("acc_spikes_shlder", "acc_spikes_shlder", "abolt_shoulder_l", "to_abolt_shoulder_l", M);
	s.skin("c", "c_black");
	ba(s, "nycpunk1");
	s.head("h_w_3_s", "f_w_3_");
	s.voice("sk", 1, 0);
}

static const RgReg reg_skinhead1[] = {S("b", "b_nycskinhead1", 3), S("a", "a_nycskinhead1", 3),
                                      S("b", "b_nycskinhead1b", 1), S("a", "a_nycskinhead1b", 1),
                                      S("h", "h_w_1_s", 0), F("f_w_1_", 0), S("h", "h_w_13_s", 1), F("f_w_13_", 1),
                                      S("h", "h_w_3_s", 3), F("f_w_3_", 3), E};
static void su_mskinhead1(RgSetup &s)
{
	s.right(11);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	if (s.half() && ba2(s, "nycskinhead1b")) {
		if (!s.racc(0.125f, "acc_hat_baseball", "acc_hat_baseball_grcamo", HB, "to_abolt_head_t_bkwd", M))
			s.racc(0.25f, "acc_hat_baseball", "acc_hat_baseball_dgrey", HB, "to_abolt_head_t_bkwd", M);
	} else {
		s.racc(0.25f, "acc_hat_baseball", "acc_hat_baseball_dgrey", HB, "to_abolt_head_t_bkwd", M);
		ba(s, "nycskinhead1");
	}
	if (s.pick(3) == 0 && s.head("h_w_1_s", "f_w_1_"))
		;
	else if (s.half() && s.head("h_w_13_s", "f_w_13_"))
		;
	else
		s.head("h_w_3_s", "f_w_3_");
	s.voice("sk", 1, 0);
	s.effect("environ/onfireburst");
	s.effect("environ/takeleak");
}

static const RgReg reg_skinhead2[] = {S("b", "b_nycskinhead2", 3), S("a", "a_nycskinhead2", 3),
                                      S("b", "b_nycskinhead2b", 1), S("a", "a_nycskinhead2b", 1),
                                      S("h", "h_w_11_c", 3), F("f_w_11_", 3), S("h", "h_w_6_s", 1), F("f_w_6_", 1),
                                      S("h", "h_w_9_s", 2), F("f_w_9_", 2), E};
static void su_mskinhead2a(RgSetup &s)
{
	s.right(3, true);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	if (!(s.half() && ba2(s, "nycskinhead2b")))
		ba(s, "nycskinhead2");
	if (s.half() && s.head("h_w_6_s", "f_w_6_"))
		;
	else if (s.pick(3) == 0 && s.head("h_w_9_s", "f_w_9_"))
		;
	else {
		s.head("h_w_11_c", "f_w_11_");
		s.off("_baldhead");
		s.on("_crewhead");
	}
	s.voice("sk", 1, 0);
	s.effect("environ/onfireburst");
}

static const RgReg reg_skinhead2b[] = {S("b", "b_nycskinhead2", 3), S("a", "a_nycskinhead2", 3),
                                       S("b", "b_nycskinhead2b", 3), S("a", "a_nycskinhead2b", 3),
                                       S("h", "h_w_11_c", 3), F("f_w_11_", 3), S("h", "h_w_6_s", 3), F("f_w_6_", 3),
                                       S("h", "h_w_9_s", 3), F("f_w_9_", 3), E};
static void su_mskinhead2b(RgSetup &s)
{
	s.right(6);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	if (s.odd())
		ba(s, "nycskinhead2");
	else
		ba(s, "nycskinhead2b");
	if (s.pick(3) == 0 && s.head("h_w_11_c", "f_w_11_")) {
		s.off("_baldhead");
		s.on("_crewhead");
	} else if (s.half() && s.head("h_w_6_s", "f_w_6_"))
		;
	else
		s.head("h_w_9_s", "f_w_9_");
	s.voice("sk", 1, 0);
	s.effect("environ/onfireburst");
}

static const RgReg reg_skinboss[] = {S("b", "b_nycskinboss", 3), S("a", "a_nycskinboss", 3), S("h", "h_w_8_s", 3),
                                     F("f_w_8_", 3), T("f_w_8_m"), E};
static void su_mskinboss(RgSetup &s)
{
	if (!s.unarmed()) {
		s.right(4);
		s.acc("acc_holster", "acc_holster_blck", HIPR, "to_abolt_hip_r", M);
	}
	ba(s, "nycskinboss");
	s.head("h_w_8_s", "f_w_8_");
	s.voice("sk", 1, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Iraq (team 1; voice so unless stated)
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_irqsoldier1[] = {S("b", "b_irqsoldier1", 3), S("a", "a_irqsoldier1", 3), S("h", "h_m_3_s", 3),
                                        F("f_m_3_", 3), S("h", "h_m_1_s", 3), F("f_m_1_", 3), E};
static void su_irq_msoldier1(RgSetup &s)
{
	s.right(7);
	s.left(22);
	s.acc("acc_hat_beret", "acc_hat_beret_red", HB, HT, M);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	s.racc(0.25f, "acc_pouch_thigh", "acc_pouch_thigh_green", THR, "to_abolt_thigh_r", M);
	s.racc(0.25f, "acc_pouch_thigh", "acc_pouch_thigh_green", THL, "to_abolt_thigh_l", M);
	s.skin("c", "c_black");
	ba(s, "irqsoldier1");
	if (!(s.odd() && s.head("h_m_3_s", "f_m_3_")))
		s.head("h_m_1_s", "f_m_1_");
	s.voice("so", 1, 0);
}

static const RgReg reg_irqsoldier2[] = {S("b", "b_irqsoldier2", 3), S("a", "a_irqsoldier2", 3), S("h", "h_m_3_s", 3),
                                        F("f_m_3_", 3), S("h", "h_m_4_c", 2), F("f_m_4_", 2), E};
static void irqsoldier2(RgSetup &s, int r)
{
	s.right(r);
	s.acc("acc_helmet", "acc_helmet_tan1", HB, HT, M);
	s.acc("acc_backpack_low", "acc_backpack_low_tan", HIPR, "to_abolt_hip_r", M);
	if (!s.racc(0.1f, "acc_pouch_thigh", "acc_pouch_thigh_tan", THR, "to_abolt_thigh_r", M))
		s.racc(0.15f, "acc_box_hip_smll", "acc_box_hip_smll_brwn", HIPR, "to_abolt_hip_r", M);
	if (!s.racc(0.1f, "acc_pouch_thigh", "acc_pouch_thigh_tan", THL, "to_abolt_thigh_l", M))
		s.racc(0.15f, "acc_box_hip_smll", "acc_box_hip_smll_brwn", HIPL, "to_abolt_hip_l", M);
	s.skin("c", "c_black");
	ba(s, "irqsoldier2");
	if (!(s.odd() && s.head("h_m_4_c", "f_m_4_")))
		s.head("h_m_3_s", "f_m_3_");
	s.voice("so", 1, 0);
}
static void su_irq_msoldier2a(RgSetup &s) { irqsoldier2(s, 6); }
static void su_irq_msoldier2b(RgSetup &s) { irqsoldier2(s, 12); }

static const RgReg reg_repguard[] = {S("b", "b_irqrepguard", 3), S("a", "a_irqrepguard", 3), S("h", "h_m_5_s", 3),
                                     F("f_m_5_", 3), S("h", "h_m_1_s", 3), F("f_m_1_", 3), E};
static void repguard(RgSetup &s, int r, int l)
{
	s.right(r);
	s.left(l);
	s.acc("acc_helmet", "acc_helmet_green1", HB, HT, M);
	s.acc("acc_backpack_smll", "acc_backpack_smll_green", BK, "to_abolt_back", M);
	s.racc(0.15f, "acc_pouch_thigh", "acc_pouch_thigh_dark1", THR, "to_abolt_thigh_r", M);
	s.racc(0.15f, "acc_box_hip_lrg", "acc_box_hip_lrg_green", HIPR, "to_abolt_hip_r", M);
	s.racc(0.15f, "acc_pouch_thigh", "acc_pouch_thigh_dark1", THL, "to_abolt_thigh_l", M);
	s.racc(0.15f, "acc_box_hip_lrg", "acc_box_hip_lrg_green", HIPL, "to_abolt_hip_l", M);
	s.skin("c", "c_black");
	ba(s, "irqrepguard");
	if (!(s.odd() && s.head("h_m_5_s", "f_m_5_")))
		s.head("h_m_1_s", "f_m_1_");
	s.voice("so", 1, 0);
}
static void su_mrepguard1a(RgSetup &s) { repguard(s, 6, 22); }
static void su_mrepguard1b(RgSetup &s) { repguard(s, 12, 24); }

static const RgReg reg_irqpolice[] = {S("b", "b_irqpolice", 3), S("a", "a_irqpolice", 3), S("h", "h_m_5_s", 3),
                                      F("f_m_5_", 3), S("h", "h_m_4_c", 1), F("f_m_4_", 1), E};
static void su_mpolice(RgSetup &s)
{
	s.right(4);
	s.acc("acc_helmet", "acc_helmet_wht1", HB, HT, M);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	s.racc(0.15f, "acc_pouch_thigh", "acc_pouch_thigh_tan", THR, "to_abolt_thigh_r", M);
	s.racc(0.15f, "acc_pouch_thigh", "acc_pouch_thigh_tan", THL, "to_abolt_thigh_l", M);
	if (!s.racc(0.1f, "acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", M))
		s.racc(0.15f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPR, "to_abolt_hip_r", M);
	s.racc(0.15f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPL, "to_abolt_hip_l", M);
	ba(s, "irqpolice");
	if (!(s.odd() && s.head("h_m_4_c", "f_m_4_")))
		s.head("h_m_5_s", "f_m_5_");
	s.voice("so", 1, 0);
}

static const RgReg reg_irqcommand[] = {S("b", "b_irqcommander", 3), S("a", "a_irqcommander", 3),
                                       S("h", "h_m_5_s", 3), F("f_m_5_", 3), S("h", "hb_h_2_b", 2), F("f_hb_1_", 2),
                                       E};
static void su_mcommander(RgSetup &s)
{
	s.right(4);
	s.acc("acc_hat_beret", "acc_hat_beret_grn", HB, HT, M);
	s.racc(0.25f, "acc_holster", "acc_holster_blck", HIPR, "to_abolt_hip_r", M);
	ba(s, "irqcommander");
	if (s.half() && s.head("hb_h_2_b", "f_hb_1_"))
		offs(s, "_baldhead", "_comface");
	else {
		s.head("h_m_5_s", "f_m_5_");
		offs(s, "_beardhead", "_beardface");
	}
	s.voice("so", 1, 0);
}

static const RgReg reg_irqbrute[] = {S("b", "b_irqbrute", 3), S("a", "a_irqbrute", 3), S("h", "h_m_5_s", 3),
                                     F("f_m_5_", 3), S("h", "h_m_3_s", 3), F("f_m_3_", 3), E};
static void irqbrutehead(RgSetup &s)
{
	ba(s, "irqbrute");
	if (!(s.odd() && s.head("h_m_5_s", "f_m_5_")))
		s.head("h_m_3_s", "f_m_3_");
	s.voice("so", 1, 0);
}
static void su_mbrute1a(RgSetup &s)
{
	s.right(6);
	s.left(22);
	s.acc("acc_backpack_lrg", "acc_backpack_lrg_tan", BK, "to_abolt_back", M);
	s.racc(0.25f, "acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", M);
	s.racc(0.15f, "acc_box_hip", "acc_box_hip_blk", HIPL, "to_abolt_hip_l", M);
	irqbrutehead(s);
	s.armour(5);
}
static void su_mbrute1b(RgSetup &s)
{
	s.right(18);
	s.acc("acc_tanks", "acc_tanks_irq", BK, "to_abolt_back", M);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	s.racc(0.25f, "acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", M);
	s.racc(0.15f, "acc_box_hip", "acc_box_hip_blk", HIPL, "to_abolt_hip_l", M);
	irqbrutehead(s);
	s.armour(4);
}

static const RgReg reg_irqbguard[] = {S("b", "b_irqbodyguard", 3), S("a", "a_irqbodyguard", 3), S("h", "h_m_1_s", 3),
                                      F("f_m_1_", 3), S("h", "h_m_4_c", 0), F("f_m_4_", 0), E};
static void su_mbodyguard(RgSetup &s)
{
	s.right(7);
	s.left(22);
	s.acc("acc_hat_beret", "acc_hat_beret_blck", HB, HT, M);
	s.racc(0.25f, "acc_holster", "acc_holster_blck", HIPR, "to_abolt_hip_r", M);
	s.racc(0.15f, "acc_box_hip", "acc_box_hip_blk", HIPL, "to_abolt_hip_l", M);
	ba(s, "irqbodyguard");
	if (!(s.odd() && s.head("h_m_4_c", "f_m_4_")))
		s.head("h_m_1_s", "f_m_1_");
	s.voice("so", 1, 0);
	s.armour(1);
}

static const RgReg reg_irqrocket[] = {S("b", "b_irqrocket", 3), S("a", "a_irqrocket", 3), S("h", "h_irqrocket", 3),
                                      S("f", "f_irqrocket", 3), E};
static void su_irq_mrocket(RgSetup &s)
{
	s.right(14);
	s.acc("acc_faceplate", "acc_faceplate_irq", HB, HT, M);
	s.racc(0.25f, "acc_backpack_smll", "acc_backpack_smll_tan", BK, "to_abolt_back", M);
	ba(s, "irqrocket");
	s.skin("f", "f_irqrocket");
	s.skin("h", "h_irqrocket");
	s.voice("so", 1, 0);
	s.armour(5);
	s.armour(3);
}

static const RgReg reg_saddam[] = {S("b", "b_irqsaddam", 3), S("a", "a_irqsaddam", 3), S("h", "h_saddam", 3),
                                   F("f_saddam_", 3), T("f_saddam_n"), E};
static void su_msaddam(RgSetup &s)
{
	s.acc("acc_hat_beret", "acc_hat_beret_blck", HB, HT, M);
	ba(s, "irqsaddam");
	s.head("h_saddam", "f_saddam_");
	s.voice("so", 1, 0);
}

static const RgReg reg_irqman2[] = {S("b", "b_irqman2a", 1), S("a", "a_irqman2a", 1), S("b", "b_irqman2b", 3),
                                    S("a", "a_irqman2b", 3), S("h", "h_m_1_s", 3), F("f_m_1_", 3),
                                    S("h", "h_m_2_s", 3), F("f_m_2_", 3), E};
static void su_mman2(RgSetup &s)
{
	if (!s.racc(0.25f, "acc_hat_turbin", "acc_hat_turbin_wht", HB, HT, M))
		if (!s.racc(0.25f, "acc_hat_top", "acc_hat_top", HB, HT, M))
			s.racc(0.333f, "acc_hat_top", "acc_hat_top_wht", HB, HT, M);
	s.racc(0.15f, "acc_machette", "acc_machette", THR, "to_abolt_thigh_r", M);
	if (!(s.half() && ba2(s, "irqman2a")))
		ba(s, "irqman2b");
	if (!(s.half() && s.head("h_m_2_s", "f_m_2_")))
		s.head("h_m_1_s", "f_m_1_");
	offs(s, "_beardhead", "_beardface");
	s.voice("npc", 1, 0);
}

static const RgReg reg_irqworker[] = {S("b", "b_irqworker", 3), S("a", "a_irqworker", 3), S("h", "h_m_2_s", 3),
                                      F("f_m_2_", 3), S("h", "h_m_1_s", 3), F("f_m_1_", 3), E};
static void su_moilworker(RgSetup &s)
{
	s.racc(0.25f, "acc_glasses", "acc_glasses_norm", HB, HT, M);
	if (!s.racc(0.1f, "acc_hat_turbin", "acc_hat_turbin_wht", HB, HT, M))
		s.racc(0.85f, "acc_hat_hard", "acc_hat_hard_wht", HB, HT, M);
	s.skin("c", "c_black");
	ba(s, "irqworker");
	if (!(s.odd() && s.head("h_m_2_s", "f_m_2_")))
		s.head("h_m_1_s", "f_m_1_");
	s.voice("npc", 1, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Sudan / Uganda (team 1)
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_ugsoldier1[] = {S("b", "b_ugnsoldier1", 3), S("a", "a_ugnsoldier1", 3), S("h", "h_b_5_s", 3),
                                       F("f_b_5_", 3), S("h", "h_b_4_c", 1), F("f_b_4_", 1), E};
static void ugsoldier1(RgSetup &s)
{
	bool hat = s.racc(0.5f, "acc_hat_flop2", "acc_hat_flop2_tan", HB, HT, M);
	s.racc(0.25f, "acc_box_hip_smll", "acc_box_hip_smll_brwn", HIPL, "to_abolt_hip_l", M);
	s.racc(0.25f, "acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", M);
	s.skin("c", "c_black");
	ba(s, "ugnsoldier1");
	if (s.odd() && s.head("h_b_4_c", "f_b_4_"))
		s.off(hat ? "_crewhead" : "_baldhead");
	else {
		s.head("h_b_5_s", "f_b_5_");
		s.off("_crewhead");
	}
	s.voice("so", 1, 0);
}
static void su_afr_msoldier1a(RgSetup &s)
{
	s.right(3);
	ugsoldier1(s);
}
static void su_afr_msoldier1b(RgSetup &s)
{
	s.right(11, true);
	s.left(22, true);
	ugsoldier1(s);
}

static const RgReg reg_ugsoldier2[] = {S("b", "b_ugnsoldier2", 3), S("a", "a_ugnsoldier2", 3), S("h", "h_b_2_s", 2),
                                       F("f_b_2_", 2), S("h", "h_b_3_s", 3), F("f_b_3_", 3), E};
static void su_afr_msoldier2(RgSetup &s)
{
	s.right(11);
	if (!s.racc(0.5f, "acc_hat_baseball", "acc_hat_baseball_tan", HB, "to_abolt_head_t_fwd", M))
		s.acc("acc_hat_flop2", "acc_hat_flop2_tan", HB, HT, M);
	s.racc(0.25f, "acc_box_hip", "acc_box_hip_canteen1", HIPL, "to_abolt_hip_l", M);
	s.racc(0.25f, "acc_box_hip_smll", "acc_box_hip_smll_brwn", HIPR, "to_abolt_hip_r", M);
	s.skin("c", "c_black");
	ba(s, "ugnsoldier2");
	if (!(s.odd() && s.head("h_b_2_s", "f_b_2_")))
		s.head("h_b_3_s", "f_b_3_");
	s.voice("so", 1, 0);
}

static const RgReg reg_ugsoldier3[] = {S("b", "b_ugnsoldier3", 3), S("a", "a_ugnsoldier3", 3), S("h", "h_b_3_s", 3),
                                       F("f_b_3_", 3), E};
static void su_afr_msoldier3(RgSetup &s)
{
	s.right(18);
	s.left(22);
	if (s.racc(0.5f, "acc_helmet2", "acc_helmet2_tan", HB, HT, M)) {
		offs(s, "_baldhead", "_comface");
		s.acc("acc_goggles", "acc_goggles", HB, HT, M);
	} else {
		offs(s, "_helmethead", "_helmetface");
		s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	}
	s.acc("acc_tanks", "acc_tanks_afr", BK, "to_abolt_back", M);
	s.skin("c", "c_black");
	ba(s, "ugnsoldier3");
	s.head("h_b_3_s", "f_b_3_");
	s.voice("so", 1, 0);
	s.armour(1);
}

static const RgReg reg_ugsniper[] = {S("b", "b_ugnsniper", 3), S("a", "a_ugnsniper", 3), S("h", "h_b_5_s", 3),
                                     F("f_b_5_", 3), S("h", "h_b_2_s", 1), F("f_b_2_", 1), E};
static void su_afr_msniper(RgSetup &s)
{
	s.right(7);
	s.left(22);
	s.acc("acc_hat_baseball", "acc_hat_baseball_tan", HB, "to_abolt_head_t_bkwd", M);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	s.racc(0.25f, "acc_box_hip_smll", "acc_box_hip_smll_brwn", HIPL, "to_abolt_hip_l", M);
	s.skin("c", "c_black");
	ba(s, "ugnsniper");
	if (!(s.odd() && s.head("h_b_2_s", "f_b_2_")))
		s.head("h_b_5_s", "f_b_5_");
	s.voice("so", 1, 0);
}

static const RgReg reg_ugbutcher[] = {S("b", "b_ugbutcher", 3), S("a", "a_ugbutcher", 3), S("h", "h_b_7_s", 2),
                                      F("f_b_7_", 2), S("h", "h_b_2_s", 3), F("f_b_2_", 3), E};
static void su_afr_mbrute(RgSetup &s)
{
	s.right(4);
	s.left(27);
	s.racc(0.5f, "acc_hat_top", "acc_hat_top_wht", HB, HT, M);
	s.skin("c", "c_black");
	ba(s, "ugbutcher");
	if (!(s.odd() && s.head("h_b_7_s", "f_b_7_")))
		s.head("h_b_2_s", "f_b_2_");
	s.voice("so", 1, 0);
	s.armour(5);
}

static const RgReg reg_ugrocket[] = {S("b", "b_ugnrocket", 3), S("a", "a_ugnrocket", 3), S("h", "h_afrrocket", 3),
                                     S("f", "f_afrrocket", 3), E};
static void su_afr_mrocket(RgSetup &s)
{
	s.right(14);
	s.acc("acc_faceplate", "acc_faceplate_afr", HB, HT, M);
	s.racc(0.25f, "acc_backpack_smll", "acc_backpack_smll_tan", BK, "to_abolt_back", M);
	s.skin("c", "c_black");
	ba(s, "ugnrocket");
	s.skin("h", "h_afrrocket");
	s.skin("f", "f_afrrocket");
	s.voice("so", 1, 0);
	s.armour(5);
	s.armour(3);
}

// ---------------------------------------------------------------------------------------------------------------
// Kosovo (team 1; voice so)
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_serbgrunt[] = {S("b", "b_kosgrunt1", 3), S("a", "a_kosgrunt1", 3),
                                      S("md", "md_serbgrunt1", 3), E};
static void su_mgrunt1(RgSetup &s)
{
	s.right(11);
	s.left(24);
	s.acc("acc_tanks", "acc_tanks_kos", BK, "to_abolt_back", M);
	s.racc(0.25f, "acc_box_hip", "acc_box_hip_canteen1", HIPL, "to_abolt_hip_l", M);
	s.racc(0.25f, "acc_puch_thigh", "acc_pouch_thigh_green", THL, "to_abolt_thigh_l", M); // sic: no such model
	s.skin("c", "c_black");
	ba(s, "kosgrunt1");
	s.skin("md", "md_serbgrunt1");
	s.voice("so", 1, 0);
}

static const RgReg reg_serbgrunt2[] = {S("b", "b_kosgrunt2", 3), S("a", "a_kosgrunt2", 3),
                                       S("md", "md_serbgrunt2", 3), E};
static void su_mgrunt2(RgSetup &s)
{
	s.right(6);
	s.left(24);
	s.acc("acc_tanks", "acc_tanks_kos", BK, "to_abolt_back", M);
	s.racc(0.25f, "acc_box_hip", "acc_box_hip_canteen1", HIPR, "to_abolt_hip_r", M);
	s.racc(0.25f, "acc_puch_thigh", "acc_pouch_thigh_tan", THL, "to_abolt_thigh_l", M); // sic
	s.skin("c", "c_black");
	ba(s, "kosgrunt2");
	s.skin("md", "md_serbgrunt2");
	s.voice("so", 1, 0);
}

static const RgReg reg_serbgrunt3[] = {S("b", "b_kosgrunt3", 3), S("a", "a_kosgrunt3", 3), S("h", "h_h_6_s", 3),
                                       F("f_h_6_", 3), S("h", "h_h_3_s", 0), F("f_h_3_", 0), S("h", "h_w_5_c", 2),
                                       F("f_w_5_", 2), E};
static void su_mgrunt3(RgSetup &s)
{
	s.right(6);
	s.acc("acc_helmet", "acc_helmet_green1", HB, HT, M);
	s.acc("acc_gas_mask2", "acc_gas_mask2a", HB, HT, M);
	s.racc(0.25f, "acc_backpack_low", "acc_backpack_low_tan", BK, "to_abolt_back", M);
	s.racc(0.25f, "acc_box_hip", "acc_box_hip_canteen1", HIPL, "to_abolt_hip_l", M);
	s.skin("c", "c_black");
	ba(s, "kosgrunt3");
	// h_w_3_s is not in the registration list: the attempt fails and the next choice is taken
	if (s.pick(3) == 0 && s.head("h_w_3_s", "f_w_3_"))
		;
	else if (s.half() && s.head("h_w_5_c", "f_w_5_"))
		;
	else
		s.head("h_h_6_s", "f_h_6_");
	s.voice("so", 1, 0);
}

static const RgReg reg_serbsniper[] = {S("b", "b_kossnipe", 3), S("a", "a_kossnipe", 3), S("h", "h_h_6_s", 3),
                                       F("f_h_6_", 3), S("h", "h_w_10_s", 1), F("f_w_10_", 1), S("h", "h_w_2_c", 0),
                                       F("f_w_2_", 0), E};
static void serbsniper(RgSetup &s, bool mask)
{
	s.right(7);
	s.left(24);
	s.acc("acc_helmet2", "acc_helmet2_green", HB, HT, M);
	if (mask)
		s.acc("acc_gas_mask2", "acc_gas_mask2a", HB, HT, M);
	s.racc(0.25f, "acc_goggles", "acc_goggles", HB, HT, M);
	s.skin("c", "c_black");
	ba(s, "kossnipe");
	if (s.pick(3) == 0 && s.head("h_w_2_c", "f_w_2_"))
		;
	else if (s.half() && s.head("h_w_10_s", "f_w_10_"))
		;
	else
		s.head("h_h_6_s", "f_h_6_");
	s.voice("so", 1, 0);
	s.armour(1);
}
static void su_msniper1a(RgSetup &s) { serbsniper(s, true); }
static void su_msniper1b(RgSetup &s) { serbsniper(s, false); }

static const RgReg reg_comtroop[] = {S("b", "b_koscomtroop", 3), S("a", "a_koscomtroop", 3), S("h", "h_w_19_s", 1),
                                     F("f_w_19_", 1), S("h", "h_w_4_s", 3), F("f_w_4_", 3), E};
static void su_mcomtroop(RgSetup &s)
{
	s.right(4);
	s.left(22);
	s.acc("acc_helmet", "acc_helmet_green1", HB, HT, M);
	s.acc("acc_gas_mask2", "acc_gas_mask2a", HB, HT, M);
	s.acc("acc_backpack_lrg", "acc_backpack_lrg_grn", BK, "to_abolt_back", M);
	s.acc("acc_antenna", "acc_antenna", BK, "to_abolt_back", M);
	s.skin("c", "c_black");
	ba(s, "koscomtroop");
	if (!(s.half() && s.head("h_w_19_s", "f_w_19_")))
		s.head("h_w_4_s", "f_w_4_");
	s.voice("so", 1, 0);
}

static const RgReg reg_serbbrute[] = {S("b", "b_kosbrute", 3), S("a", "a_kosbrute", 3), S("h", "h_h_6_s", 3),
                                      F("f_h_6_", 3), S("h", "h_w_3_s", 2), F("f_w_3_", 2), S("h", "h_w_5_c", 1),
                                      F("f_w_5_", 1), E};
static void serbbrute(RgSetup &s, bool mask)
{
	s.right(6);
	s.left(22);
	if (mask)
		s.acc("acc_gas_mask2", "acc_gas_mask2a", HB, HT, M);
	s.racc(0.1f, "acc_backpack_lrg", "acc_backpack_lrg_brwn", BK, "to_abolt_back", M);
	s.racc(0.25f, "acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", M);
	s.racc(0.25f, "acc_hat_beret", "acc_hat_beret_red", HB, HT, M);
	s.skin("c", "c_black");
	ba(s, "kosbrute");
	if (s.pick(3) == 0 && s.head("h_w_5_c", "f_w_5_"))
		;
	else if (s.half() && s.head("h_w_3_s", "f_w_3_"))
		;
	else
		s.head("h_h_6_s", "f_h_6_");
	s.voice("so", 1, 0);
	s.armour(5);
}
static void su_mbrute1a_kos(RgSetup &s) { serbbrute(s, true); }
static void su_mbrute1b_kos(RgSetup &s) { serbbrute(s, false); }

static const RgReg reg_serbmech[] = {S("b", "b_kosmechanic", 3), S("a", "a_kosmechanic", 3), S("h", "h_w_4_s", 3),
                                     F("f_w_4_", 3), S("h", "hb_h_2_b", 2), F("f_hb_1_", 2), E};
static void su_kos_mmechanic(RgSetup &s)
{
	s.acc("acc_hat_hard", "acc_hat_hard_kos", HB, HT, M);
	s.skin("c", "c_black");
	ba(s, "kosmechanic");
	if (s.half() && s.head("hb_h_2_b", "f_hb_1_"))
		offs(s, "_baldhead", "_comface");
	else {
		s.head("h_w_4_s", "f_w_4_");
		offs(s, "_beardhead", "_beardface");
	}
	s.voice("npc", 1, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Siberia (team 1; voice so unless stated)
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_sibtroop1[] = {S("b", "b_sibtrooper2", 3), S("a", "a_sibtrooper2", 3),
                                      S("hd", "hd_sibtroop", 3), S("f", "f_sibtroop", 3), E};
static void sibtroopskins(RgSetup &s, const char *f)
{
	s.skin("c", "c_black");
	ba(s, "sibtrooper2");
	s.skin("hd", "hd_sibtroop");
	s.skin("f", f);
	s.voice("so", 1, 0);
}
static void su_mtrooper1a(RgSetup &s)
{
	s.right(6);
	s.acc("acc_gas_mask", "acc_gas_mask", HB, HT, M);
	s.acc("acc_backpack_lrg", "acc_backpack_lrg_wht", BK, "to_abolt_back", M);
	s.acc("acc_antenna", "acc_antenna", BK, "to_abolt_back", M);
	sibtroopskins(s, "f_sibtroop");
}
static void su_mtrooper1b(RgSetup &s)
{
	s.right(7);
	s.acc("acc_gas_mask", "acc_gas_mask", HB, HT, M);
	s.acc("acc_backpack_smll", "acc_backpack_smll_wht", BK, "to_abolt_back", M);
	s.acc("acc_box_hip_smll", "acc_box_hip_smll_blck", HIPL, "to_abolt_hip_l", M);
	sibtroopskins(s, "f_sibtroop");
}
static const RgReg reg_sibtroop2[] = {S("b", "b_sibtrooper2", 3), S("a", "a_sibtrooper2", 3),
                                      S("hd", "hd_sibtroop", 3), S("f", "f_sibtroop2", 3), E};
static void su_mtrooper2(RgSetup &s)
{
	s.left(26);
	s.right(5);
	s.acc("acc_backpack_low", "acc_backpack_low_dgrey", HIPR, "to_abolt_hip_r", M);
	s.acc("acc_box_hip_smll", "acc_box_hip_smll_blck", HIPL, "to_abolt_hip_l", M);
	sibtroopskins(s, "f_sibtroop2");
}

static const RgReg reg_sibguard1[] = {S("b", "b_sibguard1", 3), S("a", "a_sibguard1", 3), S("h", "h_w_16_s", 3),
                                      F("f_w_16_", 3), S("h", "h_w_10_s", 1), F("f_w_10_", 1), E};
static void su_mguard1(RgSetup &s)
{
	s.left(26);
	s.right(6);
	s.racc(0.5f, "acc_hat_baseball", "acc_hat_baseball_sib", HB, HT, M);
	s.racc(0.5f, "acc_box_hip", "acc_box_hip_blk", HIPL, "to_abolt_hip_l", M);
	s.skin("c", "c_black");
	ba(s, "sibguard1");
	if (!(s.half() && s.head("h_w_10_s", "f_w_10_")))
		s.head("h_w_16_s", "f_w_16_");
	s.voice("so", 1, 0);
}

static const RgReg reg_sibguard3[] = {S("b", "b_sibguard1", 3), S("a", "a_sibguard1", 3), S("h", "h_w_16_s", 3),
                                      F("f_w_16_", 3), T("f_w_16_n"), E};
static void su_mguard3(RgSetup &s)
{
	s.left(26);
	s.right(5);
	s.acc("acc_hat_tall", "acc_hat_tall_gry", HB, HT, M);
	s.racc(0.5f, "acc_box_hip", "acc_box_hip_blk", HIPL, "to_abolt_hip_l", M);
	s.skin("c", "c_black");
	ba(s, "sibguard1");
	s.head("h_w_16_s", "f_w_16_");
	s.voice("so", 1, 0);
}

static const RgReg reg_sibguard4[] = {S("b", "b_sibguard4", 3), S("a", "a_sibguard4", 3), S("h", "h_w_21_s", 3),
                                      F("f_w_21_", 3), E};
static void su_mguard4(RgSetup &s)
{
	s.left(26);
	s.right(6);
	s.skin("c", "c_black");
	ba(s, "sibguard4");
	s.head("h_w_21_s", "f_w_21_");
	s.voice("so", 1, 0);
}

static const RgReg reg_sibclean[] = {S("b", "b_sibclean", 3), S("a", "a_sibclean", 3), S("md", "md_clean", 3), E};
static void su_mcleansuit(RgSetup &s)
{
	s.right(3);
	s.racc(0.25f, "acc_box_hip_smll", "acc_box_hip_smll_blck", HIPR, "to_abolt_hip_r", M);
	s.skin("c", "c_black");
	ba(s, "sibclean");
	s.skin("md", "md_clean");
	s.voice("cln", 1, 0);
}

static const RgReg reg_sibmech[] = {S("b", "b_sibmechanic", 3), S("a", "a_sibmechanic", 3), S("h", "h_w_19_s", 2),
                                    F("f_w_19_", 2), S("h", "hb_h_2_b", 3), F("f_hb_1_", 3), E};
static void su_sib_mmechanic(RgSetup &s)
{
	s.acc("acc_hat_hard", "acc_hat_hard_sib", HB, HT, M);
	s.skin("c", "c_black");
	ba(s, "sibmechanic");
	if (s.half() && s.head("h_w_19_s", "f_w_19_"))
		offs(s, "_beardhead", "_beardface");
	else {
		s.head("hb_h_2_b", "f_hb_1_");
		offs(s, "_baldhead", "_comface");
	}
	s.voice("npc", 1, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Tokyo (team 1 unless stated; voice ya unless stated)
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_tokhench1[] = {S("b", "b_tokhench1", 3), S("a", "a_tokhench1", 3), S("h", "h_j_5_c", 3),
                                      F("f_j_5_", 3), S("h", "h_j_2_s", 0), F("f_j_2_", 0), S("h", "h_j_6_s", 1),
                                      F("f_j_6_", 1), E};
static void su_mhench1(RgSetup &s)
{
	s.right(3);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	ba(s, "tokhench1");
	if ((s.pick(3) == 0 && s.head("h_j_2_s", "f_j_2_")) || (s.half() && s.head("h_j_6_s", "f_j_6_"))) {
		s.off("_crewhead");
		if (s.half())
			s.off("_ponytail");
	} else {
		s.head("h_j_5_c", "f_j_5_");
		s.off("_baldhead");
	}
	s.voice("ya", 1, 0);
}

static const RgReg reg_tokhench2[] = {S("b", "b_tokhench2", 3), S("a", "a_tokhench2", 3), S("h", "h_j_3_s", 2),
                                      F("f_j_3_", 2), S("h", "h_j_4_s", 3), F("f_j_4_", 3), S("h", "h_j_6_s", 1),
                                      F("f_j_6_", 1), E};
static void su_mhench2(RgSetup &s)
{
	s.right(6);
	s.racc(0.25f, "acc_glasses", "acc_glasses_sun", HB, HT, M);
	ba(s, "tokhench2");
	if (s.half() && s.head("h_j_6_s", "f_j_6_"))
		s.off("_crewhead");
	else if (s.pick(3) == 0 && s.head("h_j_3_s", "f_j_3_"))
		offs(s, "_baldhead", "_ponytail");
	else {
		s.head("h_j_4_s", "f_j_4_");
		offs(s, "_crewhead", "_ponytail");
	}
	s.voice("ya", 1, 0);
}

static const RgReg reg_tokkiller[] = {S("b", "b_tokkiller", 3), S("a", "a_tokkiller", 3), S("h", "h_j_5_c", 3),
                                      F("f_j_5_", 3), S("h", "h_j_2_s", 0), F("f_j_2_", 0), E};
static void su_mkiller(RgSetup &s)
{
	s.right(17);
	ba(s, "tokkiller");
	if (s.half() && s.head("h_j_2_s", "f_j_2_"))
		s.off("_crewhead");
	else {
		s.head("h_j_5_c", "f_j_5_");
		s.off("_baldhead");
	}
	s.voice("ya", 1, 0);
	s.armour(1);
}

static const RgReg reg_tokninja[] = {S("b", "b_tokninja", 3), S("a", "a_tokninja", 3), S("h", "h_ninja", 3),
                                     S("f", "f_ninja", 3), E};
static void su_mninja(RgSetup &s)
{
	s.left(30);
	s.right(5);
	s.acc("acc_sword", "acc_sword", BK, "to_abolt_back", M);
	ba(s, "tokninja");
	s.skin("h", "h_ninja");
	s.skin("f", "f_ninja");
	s.voice("ni", 1, 0);
}

static const RgReg reg_tokbrute[] = {S("b", "b_tokbrute", 3), S("a", "a_tokbrute", 3), S("h", "h_j_3_s", 2),
                                     F("f_j_3_", 2), S("h", "h_j_4_s", 3), F("f_j_4_", 3), E};
static void su_tok_mbrute(RgSetup &s)
{
	s.right(6);
	ba(s, "tokbrute");
	if (!(s.half() && s.head("h_j_3_s", "f_j_3_")))
		s.head("h_j_4_s", "f_j_4_");
	s.voice("ya", 1, 0);
	s.armour(5);
}

static const RgReg reg_japansuit[] = {S("b", "b_tokmhostage", 3), S("a", "a_tokmhostage", 3), S("h", "h_j_5_c", 3),
                                      F("f_j_5_", 3), S("h", "h_j_6_s", 1), F("f_j_6_", 1), E};
static void su_mman1(RgSetup &s)
{
	s.racc(0.25f, "acc_glasses", "acc_glasses_norm", HB, HT, M);
	ba(s, "tokmhostage");
	if (s.half() && s.head("h_j_6_s", "f_j_6_"))
		offs(s, "_crewhead", "_lbang");
	else {
		s.head("h_j_5_c", "f_j_5_");
		s.off("_baldhead");
		if (s.half())
			s.off("_lbang");
	}
	s.voice("npc", 1, 0);
}

static const RgReg reg_tokhostage[] = {S("b", "b_tokman1", 3), S("a", "a_tokman1", 3), S("h", "h_j_1_c", 3),
                                       F("f_j_1_", 3), S("h", "h_j_2_s", 0), F("f_j_2_", 0), E};
static void su_tok_mman2(RgSetup &s)
{
	s.racc(0.25f, "acc_glasses", "acc_glasses_norm", HB, HT, M);
	ba(s, "tokman1");
	if (s.half() && s.head("h_j_2_s", "f_j_2_"))
		offs(s, "_crewhead", "_lbang", "_rbang");
	else {
		s.head("h_j_1_c", "f_j_1_");
		s.off("_baldhead");
		if (s.half())
			s.off("_lbang");
		if (s.half())
			s.off("_rbang");
	}
	s.voice("npc", 1, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Germany (castle raiders, team 1; voice ra)
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_dcraider1[] = {S("b", "b_dcraider1", 3), S("a", "a_dcraider1", 3), S("h", "h_raider1", 3),
                                      S("f", "f_raider2", 3), E};
static void su_mraider1(RgSetup &s)
{
	s.right(12, true);
	s.acc("acc_goggles", "acc_goggles", HB, HT, M);
	s.racc(0.25f, "acc_backpack_smll", "acc_backpack_smll_dgrey", BK, "to_abolt_back", M);
	s.racc(0.3333f, "acc_box_hip", "acc_box_hip_blk", HIPL, "to_abolt_hip_l", M);
	s.racc(0.3333f, "acc_pouch_thigh", "acc_pouch_thigh_blck", THL, "to_abolt_thigh_l", M);
	s.skin("c", "c_black");
	ba(s, "dcraider1");
	s.skin("h", "h_raider1");
	s.skin("f", "f_raider2");
	s.voice("ra", 1, 0);
}

static const RgReg reg_dcraider2[] = {S("b", "b_dcraider2", 3), S("a", "a_dcraider2", 3), S("h", "h_raider2", 3),
                                      S("f", "f_raider2", 3), E};
static void raider2back(RgSetup &s)
{
	if (!s.racc(0.25f, "acc_backpack_smll", "acc_backpack_smll_dgrey", BK, "to_abolt_back", M))
		if (s.racc(0.3333f, "acc_backpack_lrg", "acc_backpack_lrg_blk", BK, "to_abolt_back", M))
			s.racc(0.5f, "acc_antenna", "acc_antenna", BK, "to_abolt_back", M);
	s.racc(0.3333f, "acc_box_hip", "acc_box_hip_blk", HIPL, "to_abolt_hip_l", M);
	s.skin("c", "c_black");
	ba(s, "dcraider2");
	s.skin("h", "h_raider2");
	s.skin("f", "f_raider2");
	s.voice("ra", 1, 0);
	s.armour(6);
}
static void su_mraider2a(RgSetup &s)
{
	s.right(5);
	s.left(22);
	s.acc("acc_nightvision2", "acc_nightvision", HB, HT, M);
	raider2back(s);
}
static void su_mraider2b(RgSetup &s)
{
	s.right(9);
	s.racc(0.25f, "acc_nightvision2", "acc_acc_nightvision", HB, HT, M); // skin name as shipped (sic)
	raider2back(s);
}

static const RgReg reg_dcbrute[] = {S("b", "b_dcbrute", 3), S("a", "a_dcbrute", 3), S("h", "h_raider4", 3),
                                    S("f", "f_raider4", 3), E};
static void su_cas_mbrute(RgSetup &s)
{
	s.right(17);
	s.left(22);
	s.skin("c", "c_black");
	ba(s, "dcbrute");
	s.skin("h", "h_raider4");
	s.skin("f", "f_raider4");
	s.voice("ra", 1, 0);
	s.armour(5);
}

static const RgReg reg_casrocket[] = {S("b", "b_casrocket", 3), S("a", "a_casrocket", 3), S("h", "h_raider3", 3),
                                      S("f", "f_raider3", 3), E};
static void su_cas_mrocket(RgSetup &s)
{
	s.right(14);
	s.acc("acc_faceplate2", "acc_faceplate2", HB, HT, M);
	s.skin("c", "c_black");
	ba(s, "casrocket");
	s.skin("h", "h_raider3");
	s.skin("f", "f_raider3");
	s.voice("ra", 1, 0);
	s.armour(5);
	s.armour(3);
}

// ---------------------------------------------------------------------------------------------------------------
// bosses (SPEC-732 §4)
// ---------------------------------------------------------------------------------------------------------------
static const RgReg reg_amu[] = {S("b", "b_xamu", 3), S("a", "a_xamu", 3), S("h", "h_amu", 3), F("f_amu_", 3),
                                T("f_amu_f"), T("f_amu_n"), E};
static void su_miraqboss(RgSetup &s)
{
	s.acc("acc_holster", "acc_holster_brwn", HIPR, "to_abolt_hip_r", M);
	s.acc("acc_hat_beret", "acc_hat_beret_red", HB, HT, 8.0f / 7.0f + 0.04f);   // 1.182857
	s.acc("acc_amu_head", "rubbish", HB, HT, 8.0f / 7.0f + 0.045f);       // 1.187857
	ba(s, "xamu");
	s.head("h_amu", "f_amu_");
	s.voice("soldier", 2, 0);
}

static const RgReg reg_dekker[] = {S("b", "b_xdekker", 3), S("a", "a_xdekker", 3), S("h", "h_dekker", 3),
                                   S("f", "f_dekker", 3), E};
static void dekkerskins(RgSetup &s)
{
	ba(s, "xdekker");
	s.skin("h", "h_dekker");
	s.skin("f", "f_dekker");
	s.voice("soldier", 2, 0);
}
static void su_mraiderboss1(RgSetup &s)
{
	s.right(4);
	dekkerskins(s);
	s.armour(7);
}
static void su_mraiderboss2(RgSetup &s)
{
	if (s.ai)
		s.ai->step_height = 32;
	if (s.inst)
		s.inst->SetSpeed(4); // playback-speed index 4 (normal)
	if (!s.unarmed()) {
		s.right(29);
		s.left(22);
	}
	dekkerskins(s);
	s.ent->flags |= FL_NO_KNOCKBACK;
	// the entity flag that lets shots pass his held weapons (SPEC-732 §4) is read by the body package (G3)
	s.armour(5);
	s.armour(7);
	s.effect("environ/dekarmor1");
}

#define MESO(cn, t, set, ss, reg, su) {cn, t, RG_FAM_MESO, set, NULL, ss, 1.0f, false, reg, su}
#define MESOA(cn, t, set, ss, reg, su) {cn, t, RG_FAM_MESO, set, NULL, ss, 1.0f, true, reg, su}
const RgHumanType rg_meso_types[] = {
	MESO("m_x_mcharacter", 31, "john", RG_SET_B, reg_john, su_john),
	MESO("m_x_mcharacter_snow", 32, "johnsnow", RG_SET_B, reg_johnsnow, su_johnsnow),
	MESO("m_x_mcharacter_desert", 33, "johndesert", RG_SET_B, reg_johndesert, su_johndesert),
	MESO("m_x_mmerc", 34, "hawk", RG_SET_B, reg_hawk, su_mmerc),
	MESO("m_x_mhurtmerc", 35, "hawk", RG_SET_B, reg_hawk, su_mhurtmerc),
	MESO("m_x_msam", 36, "sam", RG_SET_B, reg_sam, su_msam),
	MESO("m_irq_moilworker", 37, "irqworker", RG_SET_A, reg_irqworker, su_moilworker),
	MESO("m_nyc_mpunk", 38, "nypunk", RG_SET_B, reg_nypunk, su_mpunk),
	MESO("m_x_miraqboss", 39, "amu", RG_SET_C, reg_amu, su_miraqboss),
	{"m_x_mraiderboss1", 40, RG_FAM_MESO_ARMOURED, "dekker1", NULL, RG_SET_C, 1.1f, false, reg_dekker,
	 su_mraiderboss1},
	{"m_x_mraiderboss2", 41, RG_FAM_MESO_ARMOURED, "dekker2", NULL, RG_SET_G, 1.1f, true, reg_dekker,
	 su_mraiderboss2},
	MESO("m_irq_msoldier1", 42, "irqsoldier1", RG_SET_B, reg_irqsoldier1, su_irq_msoldier1),
	MESO("m_irq_msoldier2a", 43, "irqsoldier2", RG_SET_B, reg_irqsoldier2, su_irq_msoldier2a),
	MESO("m_irq_msoldier2b", 44, "irqsoldier2b", RG_SET_B, reg_irqsoldier2, su_irq_msoldier2b),
	MESO("m_irq_mrepguard1a", 45, "irqrepgd", RG_SET_B, reg_repguard, su_mrepguard1a),
	MESO("m_irq_mrepguard1b", 46, "irqrepgdb", RG_SET_B, reg_repguard, su_mrepguard1b),
	MESO("m_irq_mpolice", 47, "irqpolice", RG_SET_C, reg_irqpolice, su_mpolice),
	MESO("m_irq_mcommander", 48, "irqcommand", RG_SET_C, reg_irqcommand, su_mcommander),
	MESO("m_irq_mbrute1a", 49, "irqbrute", RG_SET_C, reg_irqbrute, su_mbrute1a),
	MESO("m_irq_mbrute1b", 50, "irqbruteb", RG_SET_C, reg_irqbrute, su_mbrute1b),
	MESOA("m_irq_mbodyguard", 51, "irqbguard", RG_SET_C, reg_irqbguard, su_mbodyguard),
	MESOA("m_irq_mrocket", 52, "irqrocket", RG_SET_C, reg_irqrocket, su_irq_mrocket),
	MESO("m_irq_msaddam", 53, "saddam", RG_SET_C, reg_saddam, su_msaddam),
	MESO("m_irq_mman2", 54, "irqman2", RG_SET_C, reg_irqman2, su_mman2),
	MESO("m_afr_msniper", 55, "ugsniper", RG_SET_C, reg_ugsniper, su_afr_msniper),
	MESO("m_x_romulan", 55, "romulan", RG_SET_A, reg_ugsniper, su_afr_msniper),
	MESO("m_afr_msoldier1a", 56, "ugsoldier1", RG_SET_B, reg_ugsoldier1, su_afr_msoldier1a),
	MESO("m_afr_msoldier1b", 57, "ugsoldier1b", RG_SET_B, reg_ugsoldier1, su_afr_msoldier1b),
	MESO("m_afr_msoldier2", 58, "ugsoldier2", RG_SET_B, reg_ugsoldier2, su_afr_msoldier2),
	MESOA("m_afr_msoldier3", 59, "ugsoldier3", RG_SET_B, reg_ugsoldier3, su_afr_msoldier3),
	MESOA("m_nyc_mswat", 60, "nyswatguy", RG_SET_B, reg_swat, su_mswat),
	MESOA("m_nyc_mswatleader", 61, "nyswatlead", RG_SET_B, reg_swatlead, su_mswatleader),
	MESO("m_cas_mraider1", 62, "dcraider1", RG_SET_B, reg_dcraider1, su_mraider1),
	MESOA("m_cas_mraider2a", 63, "dcraider2", RG_SET_B, reg_dcraider2, su_mraider2a),
	MESOA("m_cas_mraider2b", 64, "dcraider2b", RG_SET_B, reg_dcraider2, su_mraider2b),
	MESO("m_cas_mbrute", 65, "dcraiderbrute", RG_SET_B, reg_dcbrute, su_cas_mbrute),
	MESOA("m_cas_mrocket", 66, "dcraiderrocket", RG_SET_B, reg_casrocket, su_cas_mrocket),
	MESO("m_sib_mtrooper2", 67, "sibtroop2", RG_SET_B, reg_sibtroop2, su_mtrooper2),
	MESO("m_sib_mcleansuit", 68, "sibclean", RG_SET_B, reg_sibclean, su_mcleansuit),
	MESO("m_kos_mgrunt1", 69, "serbgrunt", RG_SET_B, reg_serbgrunt, su_mgrunt1),
	MESO("m_kos_mgrunt2", 70, "serbgrunt2", RG_SET_B, reg_serbgrunt2, su_mgrunt2),
	MESO("m_kos_mgrunt3", 71, "serbgrunt3", RG_SET_B, reg_serbgrunt3, su_mgrunt3),
	MESOA("m_kos_msniper1a", 72, "serbsniper", RG_SET_B, reg_serbsniper, su_msniper1a),
	MESOA("m_kos_msniper1b", 73, "serbsniper", RG_SET_B, reg_serbsniper, su_msniper1b),
	MESO("m_kos_mcomtroop", 74, "serbcomtroop", RG_SET_B, reg_comtroop, su_mcomtroop),
	MESO("m_kos_mbrute1a", 75, "serbbrute", RG_SET_B, reg_serbbrute, su_mbrute1a_kos),
	MESO("m_kos_mbrute1b", 76, "serbbrute", RG_SET_B, reg_serbbrute, su_mbrute1b_kos),
	MESO("m_kos_mmechanic", 77, "serbmechanic", RG_SET_A, reg_serbmech, su_kos_mmechanic),
	MESO("m_kos_mrebel", 78, "kosrebel", RG_SET_B, reg_kosrebel, su_mrebel),
	MESO("m_kos_mklaleader", 79, "kosklaguy", RG_SET_B, reg_klaleader, su_mklaleader),
	MESO("m_nyc_mskinhead1", 80, "skinhead1", RG_SET_B, reg_skinhead1, su_mskinhead1),
	MESO("m_nyc_mskinhead2a", 81, "skinhead2", RG_SET_B, reg_skinhead2, su_mskinhead2a),
	MESO("m_nyc_mskinhead2b", 82, "skinhead2b", RG_SET_B, reg_skinhead2b, su_mskinhead2b),
	{"m_x_mskinboss", 83, RG_FAM_MESO, "skinboss", NULL, RG_SET_C, 1.1f, false, reg_skinboss, su_mskinboss},
	MESO("m_nyc_mpolitician", 84, "politician", RG_SET_A, reg_politician, su_mpolitician),
	MESO("m_tok_mman2", 85, "tokhostage", RG_SET_A, reg_tokhostage, su_tok_mman2),
	MESO("m_tok_mhench1", 86, "tokhench1", RG_SET_B, reg_tokhench1, su_mhench1),
	MESO("m_tok_mhench2", 87, "tokhench2", RG_SET_B, reg_tokhench2, su_mhench2),
	MESOA("m_tok_mkiller", 88, "tokkiller", RG_SET_B, reg_tokkiller, su_mkiller),
	MESO("m_tok_mninja", 89, "tokninja", RG_SET_B, reg_tokninja, su_mninja),
	MESO("m_tok_mbrute", 90, "tokbrute", RG_SET_B, reg_tokbrute, su_tok_mbrute),
	MESO("m_tok_mman1", 91, "japansuit", RG_SET_A, reg_japansuit, su_mman1),
	MESO("m_afr_mbrute", 92, "butch", RG_SET_C, reg_ugbutcher, su_afr_mbrute),
	MESOA("m_afr_mrocket", 93, "ugrocket", RG_SET_C, reg_ugrocket, su_afr_mrocket),
	MESO("m_sib_mtrooper1a", 94, "sibtroop1a", RG_SET_B, reg_sibtroop1, su_mtrooper1a),
	MESO("m_sib_mtrooper1b", 95, "sibtroop1b", RG_SET_B, reg_sibtroop1, su_mtrooper1b),
	MESO("m_sib_mguard1", 96, "sibguard", RG_SET_B, reg_sibguard1, su_mguard1),
	MESO("m_sib_mguard3", 97, "sibguard3", RG_SET_B, reg_sibguard3, su_mguard3),
	MESO("m_sib_mguard4", 98, "sibguard4", RG_SET_B, reg_sibguard4, su_mguard4),
	MESO("m_sib_mmechanic", 99, "sibmech", RG_SET_B, reg_sibmech, su_sib_mmechanic),
	{NULL, 0, 0, NULL, NULL, 0, 0, false, NULL, NULL}};
