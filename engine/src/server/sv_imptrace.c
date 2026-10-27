/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc server: import-call ring (a test aid, not in the original). With
 * cvar sv_watchdog > 0 when the game module is loaded, every function entry of
 * the import table the game receives (SPEC-001, 99 entries) is replaced by a
 * small thunk that records the entry number in a ring of the last calls and
 * jumps to the engine's function. The method tables of the OpenMerc Ghoul
 * library's system, object, instance and player-model classes (SPEC-010/011)
 * are patched the same way once an object of each is seen, so calls the game
 * makes through those interfaces are recorded too. The crash and stall reports
 * of sv_watchdog.c print the ring, so a fault inside the game module can be put
 * next to the engine services it used last (black box: nothing about the game's
 * own code is recorded). MSVC x86 only (naked thunks); elsewhere nothing is
 * patched.
 *
 * Implements: SPEC-001 host diagnostics (no game behaviour)
 */

#include <stdio.h>

#include "header/server.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define IMP_COUNT 99
#define IMP_RING 1024

static const char *const imp_names[IMP_COUNT] = {
	"modelindex", "soundindex", "effectindex", "imageindex", "soundremove", "addressbanned",
	"ghoulregisterall", "setmodel", "setrendermodel", "argc", "argv", "args", "bprintf", "dprintf",
	"cprintf", "clprintf", "welcomeprint", "centerprintf", "cinprintf", "captionprint",
	"captionprintent", "forceconnotify", "sound", "positioned_sound", "unused24", "damagetexture",
	"getmaterials", "loadingprogress", "multicast", "multicastexclude", "unicast", "WriteChar",
	"WriteByte", "WriteShort", "WriteLong", "WriteFloat", "WriteString", "WritePosition", "WriteDir",
	"WriteAngle", "SZ_WriteByte", "SZ_WriteShort", "SZ_WriteLong", "ReliableWriteByteToClient",
	"ReliableWriteDataToClient", "DirToByte", "SendPlayernameColors", "SP_Register", "SP_Print",
	"SP_Print_Obit", "SP_SPrint", "SP_GetStringText", "trace", "tracefilter", "pointcontents",
	"regiondistance", "inPVS", "inPHS", "SetAreaPortalState", "AreasConnected", "GetGhoul",
	"NewPlayerModelInfo", "LoadGhoulFile", "ReadLine", "PreloadGSQ", "RegisterGSQSequences",
	"TurnOffPartsFromGSQ", "playercontext", "configstring", "SZ_Init", "SZ_Clear", "SZ_Write", "error",
	"dedicatedprint", "LoadPlayerDLL", "UnloadPlayerDLL", "flrand", "irand", "linkentity",
	"unlinkentity", "BoxEdicts", "Pmove", "TagMalloc", "TagFree", "FreeTags", "WriteChunk",
	"ReadChunk", "cvar", "cvar_set", "cvar_setvalue", "cvar_forceset", "cvar_info", "cvar_value",
	"FS_LoadFile", "FS_FreeFile", "FS_Gamedir", "FS_CreatePath", "FS_FileExists", "AddCommandString"
};

static volatile unsigned imp_ring[IMP_RING];
static volatile unsigned imp_pos;

static void *vt_orig[512];
static void **vt_hooked[8];

#if defined(_MSC_VER) && defined(_M_IX86)
/* records the code, then continues in the library's method (thiscall: this in
   ecx, untouched; eax is free) */
#define VT_THUNK(i, code) \
	static __declspec(naked) void vt_thunk_##i(void) \
	{ \
		__asm mov eax, imp_pos \
		__asm and eax, (IMP_RING - 1) \
		__asm mov dword ptr imp_ring[eax * 4], code \
		__asm inc dword ptr imp_pos \
		__asm jmp dword ptr vt_orig[i * 4] \
	}
#endif

/* BEGIN generated vtable thunks */
/* generated from ghoul_api.h (interfaces of SPEC-010/011): 152 methods */
#define VT_COUNT 152
static const struct { const char *iface; int first, count, code; } vt_ifaces[] = {
	{"system", 0, 49, 1000},
	{"object", 49, 29, 1100},
	{"instance", 78, 55, 1200},
	{"playermodel", 133, 19, 1300},
};
static const char *const vt_names[VT_COUNT] = {
	"system.CreateObject", "system.SetGLTable", "system.SetSpecular", "system.ReleaseTextures",
	"system.BeginFrame", "system.EndFrame", "system.DestroyAllObjects", "system.Purge",
	"system.Reset", "system.FindInstance", "system.FindObject", "system.SetFullPathCallback",
	"system.NetClientConnected", "system.NetClientDropped", "system.NetResendAll", "system.NetDropAll",
	"system.NetWriteReliable", "system.NetReliablePending", "system.NetWriteUnreliable", "system.NetAcknowledge",
	"system.NetCommitFrame", "system.NetBeginFrame", "system.TouchFile", "system.ClientStartAccepting",
	"system.ClientReadReliable", "system.ClientReadUnreliable", "system.ClientReliableSize", "system.ClientReliableData",
	"system.ModelLightCount", "system.GetModelLight", "system.ClearLights", "system.AddDynamicLight",
	"system.EndRenderFrame", "system.FlushMeshCache", "system.AddWorldLight", "system.FinishWorldLights",
	"system.BeginWorldLights", "system.SetGamma", "system.SetTextureDetail", "system.SetPolygonOffset",
	"system.StatTextures", "system.StatTextureBytes", "system.StatMeshes", "system.StatTriangles",
	"system.StatTrianglesTotal", "system.StatVertices", "system.StatCorners", "system.StatLights",
	"system.ResetStats", "object.RegisterSequence", "object.FinishRegistration", "object.Prepare",
	"object.FindSequence", "object.NumSequences", "object.GetSequencePath", "object.RegisterPart",
	"object.FindPart", "object.NumParts", "object.PartType", "object.GetPartName",
	"object.RegisterNote", "object.FindNote", "object.NumNotes", "object.EnumerateNotes",
	"object.RegisterMaterial", "object.FindMaterial", "object.NumMaterials", "object.GetMaterialName",
	"object.RegisterSkin", "object.FindSkin", "object.NumSkins", "object.GetSkinName",
	"object.SkinMaterial", "object.CreateInstance", "object.GetObjectId", "object.GetSystem",
	"object.Destroy", "object.DestroyAllInstances", "instance.SetTransform", "instance.SetTransform16",
	"instance.GetTransform", "instance.GetTransform16", "instance.SetOnOff", "instance.IsOn",
	"instance.SetSpeed", "instance.GetSpeed", "instance.SetPartOnOff", "instance.IsPartOn",
	"instance.SetAllPartsOnOff", "instance.SetSkin", "instance.GetSkin", "instance.RemoveSkin",
	"instance.SetTint", "instance.GetTint", "instance.SetTintAll", "instance.Play",
	"instance.GetPlayingSequence", "instance.Pause", "instance.Resume", "instance.SetUserValue",
	"instance.GetUserValue", "instance.AddNoteCallback", "instance.RemoveNoteCallback", "instance.AddMatrixCallback",
	"instance.RemoveMatrixCallback", "instance.SequenceDuration", "instance.AttachChild", "instance.SetAttachBolt",
	"instance.GetAttachBolt", "instance.DetachChild", "instance.NumChildren", "instance.GetChild",
	"instance.GetGhoulObject", "instance.GetParent", "instance.Clone", "instance.SaveState",
	"instance.LoadState", "instance.GetInstanceId", "instance.GetObjectId", "instance.Draw",
	"instance.Reserved42", "instance.TraceRay", "instance.TraceBox", "instance.ProcessEvents",
	"instance.StopEvents", "instance.StopMatrixEvents", "instance.StartNoteEvents", "instance.StartEvents",
	"instance.GetBoltMatrix", "instance.GetBounds", "instance.NetOffer", "instance.Destroy",
	"instance.GetPlayingSequence2", "playermodel.Load", "playermodel.GetUserValue", "playermodel.SetUserValue",
	"playermodel.NumSkins", "playermodel.NumBoltons", "playermodel.NumPartSwitches", "playermodel.GetModelDir",
	"playermodel.GetMenuSequence", "playermodel.GetBaseModel", "playermodel.GetSequenceList", "playermodel.GetFacePrefix",
	"playermodel.GetTrailer", "playermodel.GetTeam", "playermodel.GetSoundDir", "playermodel.GetSkinEntry",
	"playermodel.GetBoltonEntry", "playermodel.GetPartEntry", "playermodel.IsLoaded", "playermodel.Destroy",
};
#if defined(_MSC_VER) && defined(_M_IX86)
VT_THUNK(0, 1000) VT_THUNK(1, 1001) VT_THUNK(2, 1002) VT_THUNK(3, 1003)
VT_THUNK(4, 1004) VT_THUNK(5, 1005) VT_THUNK(6, 1006) VT_THUNK(7, 1007)
VT_THUNK(8, 1008) VT_THUNK(9, 1009) VT_THUNK(10, 1010) VT_THUNK(11, 1011)
VT_THUNK(12, 1012) VT_THUNK(13, 1013) VT_THUNK(14, 1014) VT_THUNK(15, 1015)
VT_THUNK(16, 1016) VT_THUNK(17, 1017) VT_THUNK(18, 1018) VT_THUNK(19, 1019)
VT_THUNK(20, 1020) VT_THUNK(21, 1021) VT_THUNK(22, 1022) VT_THUNK(23, 1023)
VT_THUNK(24, 1024) VT_THUNK(25, 1025) VT_THUNK(26, 1026) VT_THUNK(27, 1027)
VT_THUNK(28, 1028) VT_THUNK(29, 1029) VT_THUNK(30, 1030) VT_THUNK(31, 1031)
VT_THUNK(32, 1032) VT_THUNK(33, 1033) VT_THUNK(34, 1034) VT_THUNK(35, 1035)
VT_THUNK(36, 1036) VT_THUNK(37, 1037) VT_THUNK(38, 1038) VT_THUNK(39, 1039)
VT_THUNK(40, 1040) VT_THUNK(41, 1041) VT_THUNK(42, 1042) VT_THUNK(43, 1043)
VT_THUNK(44, 1044) VT_THUNK(45, 1045) VT_THUNK(46, 1046) VT_THUNK(47, 1047)
VT_THUNK(48, 1048) VT_THUNK(49, 1100) VT_THUNK(50, 1101) VT_THUNK(51, 1102)
VT_THUNK(52, 1103) VT_THUNK(53, 1104) VT_THUNK(54, 1105) VT_THUNK(55, 1106)
VT_THUNK(56, 1107) VT_THUNK(57, 1108) VT_THUNK(58, 1109) VT_THUNK(59, 1110)
VT_THUNK(60, 1111) VT_THUNK(61, 1112) VT_THUNK(62, 1113) VT_THUNK(63, 1114)
VT_THUNK(64, 1115) VT_THUNK(65, 1116) VT_THUNK(66, 1117) VT_THUNK(67, 1118)
VT_THUNK(68, 1119) VT_THUNK(69, 1120) VT_THUNK(70, 1121) VT_THUNK(71, 1122)
VT_THUNK(72, 1123) VT_THUNK(73, 1124) VT_THUNK(74, 1125) VT_THUNK(75, 1126)
VT_THUNK(76, 1127) VT_THUNK(77, 1128) VT_THUNK(78, 1200) VT_THUNK(79, 1201)
VT_THUNK(80, 1202) VT_THUNK(81, 1203) VT_THUNK(82, 1204) VT_THUNK(83, 1205)
VT_THUNK(84, 1206) VT_THUNK(85, 1207) VT_THUNK(86, 1208) VT_THUNK(87, 1209)
VT_THUNK(88, 1210) VT_THUNK(89, 1211) VT_THUNK(90, 1212) VT_THUNK(91, 1213)
VT_THUNK(92, 1214) VT_THUNK(93, 1215) VT_THUNK(94, 1216) VT_THUNK(95, 1217)
VT_THUNK(96, 1218) VT_THUNK(97, 1219) VT_THUNK(98, 1220) VT_THUNK(99, 1221)
VT_THUNK(100, 1222) VT_THUNK(101, 1223) VT_THUNK(102, 1224) VT_THUNK(103, 1225)
VT_THUNK(104, 1226) VT_THUNK(105, 1227) VT_THUNK(106, 1228) VT_THUNK(107, 1229)
VT_THUNK(108, 1230) VT_THUNK(109, 1231) VT_THUNK(110, 1232) VT_THUNK(111, 1233)
VT_THUNK(112, 1234) VT_THUNK(113, 1235) VT_THUNK(114, 1236) VT_THUNK(115, 1237)
VT_THUNK(116, 1238) VT_THUNK(117, 1239) VT_THUNK(118, 1240) VT_THUNK(119, 1241)
VT_THUNK(120, 1242) VT_THUNK(121, 1243) VT_THUNK(122, 1244) VT_THUNK(123, 1245)
VT_THUNK(124, 1246) VT_THUNK(125, 1247) VT_THUNK(126, 1248) VT_THUNK(127, 1249)
VT_THUNK(128, 1250) VT_THUNK(129, 1251) VT_THUNK(130, 1252) VT_THUNK(131, 1253)
VT_THUNK(132, 1254) VT_THUNK(133, 1300) VT_THUNK(134, 1301) VT_THUNK(135, 1302)
VT_THUNK(136, 1303) VT_THUNK(137, 1304) VT_THUNK(138, 1305) VT_THUNK(139, 1306)
VT_THUNK(140, 1307) VT_THUNK(141, 1308) VT_THUNK(142, 1309) VT_THUNK(143, 1310)
VT_THUNK(144, 1311) VT_THUNK(145, 1312) VT_THUNK(146, 1313) VT_THUNK(147, 1314)
VT_THUNK(148, 1315) VT_THUNK(149, 1316) VT_THUNK(150, 1317) VT_THUNK(151, 1318)
static void (*const vt_thunks[VT_COUNT])(void) = {
	vt_thunk_0, vt_thunk_1, vt_thunk_2, vt_thunk_3, vt_thunk_4, vt_thunk_5, vt_thunk_6, vt_thunk_7,
	vt_thunk_8, vt_thunk_9, vt_thunk_10, vt_thunk_11, vt_thunk_12, vt_thunk_13, vt_thunk_14, vt_thunk_15,
	vt_thunk_16, vt_thunk_17, vt_thunk_18, vt_thunk_19, vt_thunk_20, vt_thunk_21, vt_thunk_22, vt_thunk_23,
	vt_thunk_24, vt_thunk_25, vt_thunk_26, vt_thunk_27, vt_thunk_28, vt_thunk_29, vt_thunk_30, vt_thunk_31,
	vt_thunk_32, vt_thunk_33, vt_thunk_34, vt_thunk_35, vt_thunk_36, vt_thunk_37, vt_thunk_38, vt_thunk_39,
	vt_thunk_40, vt_thunk_41, vt_thunk_42, vt_thunk_43, vt_thunk_44, vt_thunk_45, vt_thunk_46, vt_thunk_47,
	vt_thunk_48, vt_thunk_49, vt_thunk_50, vt_thunk_51, vt_thunk_52, vt_thunk_53, vt_thunk_54, vt_thunk_55,
	vt_thunk_56, vt_thunk_57, vt_thunk_58, vt_thunk_59, vt_thunk_60, vt_thunk_61, vt_thunk_62, vt_thunk_63,
	vt_thunk_64, vt_thunk_65, vt_thunk_66, vt_thunk_67, vt_thunk_68, vt_thunk_69, vt_thunk_70, vt_thunk_71,
	vt_thunk_72, vt_thunk_73, vt_thunk_74, vt_thunk_75, vt_thunk_76, vt_thunk_77, vt_thunk_78, vt_thunk_79,
	vt_thunk_80, vt_thunk_81, vt_thunk_82, vt_thunk_83, vt_thunk_84, vt_thunk_85, vt_thunk_86, vt_thunk_87,
	vt_thunk_88, vt_thunk_89, vt_thunk_90, vt_thunk_91, vt_thunk_92, vt_thunk_93, vt_thunk_94, vt_thunk_95,
	vt_thunk_96, vt_thunk_97, vt_thunk_98, vt_thunk_99, vt_thunk_100, vt_thunk_101, vt_thunk_102, vt_thunk_103,
	vt_thunk_104, vt_thunk_105, vt_thunk_106, vt_thunk_107, vt_thunk_108, vt_thunk_109, vt_thunk_110, vt_thunk_111,
	vt_thunk_112, vt_thunk_113, vt_thunk_114, vt_thunk_115, vt_thunk_116, vt_thunk_117, vt_thunk_118, vt_thunk_119,
	vt_thunk_120, vt_thunk_121, vt_thunk_122, vt_thunk_123, vt_thunk_124, vt_thunk_125, vt_thunk_126, vt_thunk_127,
	vt_thunk_128, vt_thunk_129, vt_thunk_130, vt_thunk_131, vt_thunk_132, vt_thunk_133, vt_thunk_134, vt_thunk_135,
	vt_thunk_136, vt_thunk_137, vt_thunk_138, vt_thunk_139, vt_thunk_140, vt_thunk_141, vt_thunk_142, vt_thunk_143,
	vt_thunk_144, vt_thunk_145, vt_thunk_146, vt_thunk_147, vt_thunk_148, vt_thunk_149, vt_thunk_150, vt_thunk_151,
};
#endif
/* END generated vtable thunks */

#if defined(_MSC_VER) && defined(_M_IX86)

static void *imp_real[IMP_COUNT + 1];

/* records n, then continues in the engine's function with the caller's stack
   and registers untouched (eax is free at a cdecl call) */
#define IMP_THUNK(n) \
	static __declspec(naked) void imp_thunk_##n(void) \
	{ \
		__asm mov eax, imp_pos \
		__asm and eax, (IMP_RING - 1) \
		__asm mov dword ptr imp_ring[eax * 4], n \
		__asm inc dword ptr imp_pos \
		__asm jmp dword ptr imp_real[n * 4] \
	}

#define IMP_T10(a) IMP_THUNK(a##0) IMP_THUNK(a##1) IMP_THUNK(a##2) IMP_THUNK(a##3) IMP_THUNK(a##4) \
	IMP_THUNK(a##5) IMP_THUNK(a##6) IMP_THUNK(a##7) IMP_THUNK(a##8) IMP_THUNK(a##9)

IMP_THUNK(0) IMP_THUNK(1) IMP_THUNK(2) IMP_THUNK(3) IMP_THUNK(4) IMP_THUNK(5) IMP_THUNK(6) IMP_THUNK(7)
IMP_THUNK(8) IMP_THUNK(9)
IMP_T10(1) IMP_T10(2) IMP_T10(3) IMP_T10(4) IMP_T10(5) IMP_T10(6) IMP_T10(7) IMP_T10(8) IMP_T10(9)

#define IMP_P10(a) imp_thunk_##a##0, imp_thunk_##a##1, imp_thunk_##a##2, imp_thunk_##a##3, \
	imp_thunk_##a##4, imp_thunk_##a##5, imp_thunk_##a##6, imp_thunk_##a##7, imp_thunk_##a##8, \
	imp_thunk_##a##9

static void (*const imp_thunks[IMP_COUNT + 1])(void) = {
	imp_thunk_0, imp_thunk_1, imp_thunk_2, imp_thunk_3, imp_thunk_4, imp_thunk_5, imp_thunk_6,
	imp_thunk_7, imp_thunk_8, imp_thunk_9,
	IMP_P10(1), IMP_P10(2), IMP_P10(3), IMP_P10(4), IMP_P10(5), IMP_P10(6), IMP_P10(7), IMP_P10(8),
	IMP_P10(9)
};

void
SV_ImportTrace_Install(void *table)
{
	void **entries = (void **)table;
	int i;

	for (i = 0; i < IMP_COUNT; i++)
	{
		/* 24 is unused, 67 is a data pointer (SPEC-001) */
		if ((i == 24) || (i == 67) || !entries[i])
		{
			continue;
		}

		imp_real[i] = entries[i];
		entries[i] = (void *)imp_thunks[i];
	}

	Com_Printf("sv_watchdog: game imports traced\n");
}

/* iface: 0 system, 1 object, 2 instance, 3 player model (vt_ifaces) */
void
SV_ImportTrace_HookVtable(int iface, void *object)
{
	void **vtbl;
	DWORD old;
	int i, first, count;

	if (!object || (iface < 0) || (iface >= (int)(sizeof(vt_ifaces) / sizeof(vt_ifaces[0]))))
	{
		return;
	}

	vtbl = *(void ***)object;

	if (vt_hooked[iface] || !imp_pos)
	{
		return; /* one class per interface; only while imports are traced */
	}

	first = vt_ifaces[iface].first;
	count = vt_ifaces[iface].count;

	if (!VirtualProtect(vtbl, count * sizeof(void *), PAGE_READWRITE, &old))
	{
		return;
	}

	for (i = 0; i < count; i++)
	{
		vt_orig[first + i] = vtbl[i];
		vtbl[i] = (void *)vt_thunks[first + i];
	}

	VirtualProtect(vtbl, count * sizeof(void *), old, &old);
	vt_hooked[iface] = vtbl;
	Com_Printf("sv_watchdog: Ghoul %s methods traced\n", vt_ifaces[iface].iface);
}

#else

void
SV_ImportTrace_Install(void *table)
{
	(void)table;
}

void
SV_ImportTrace_HookVtable(int iface, void *object)
{
	(void)iface;
	(void)object;
}

#endif

static const char *
SV_ImportTrace_Name(unsigned n)
{
	int k;

	if (n < IMP_COUNT)
	{
		return imp_names[n];
	}

	for (k = 0; k < (int)(sizeof(vt_ifaces) / sizeof(vt_ifaces[0])); k++)
	{
		if ((n >= (unsigned)vt_ifaces[k].code) && (n < (unsigned)(vt_ifaces[k].code + vt_ifaces[k].count)))
		{
			return vt_names[vt_ifaces[k].first + (n - vt_ifaces[k].code)];
		}
	}

	return NULL;
}

/* the last calls, oldest first, runs of the same entry folded */
void
SV_ImportTrace_Dump(void *file)
{
	FILE *f = (FILE *)file;
	unsigned end = imp_pos, start = (end > IMP_RING) ? end - IMP_RING : 0, i;
	unsigned last = (unsigned)-1, run = 0;

	if (!end)
	{
		return;
	}

	fprintf(f, " last game imports (#) and Ghoul calls (g), oldest first, %u calls in all:\n", end);

	for (i = start; i <= end; i++)
	{
		unsigned n = (i < end) ? imp_ring[i & (IMP_RING - 1)] : (unsigned)-2;

		if (n == last)
		{
			run++;
			continue;
		}

		if (SV_ImportTrace_Name(last))
		{
			if (run > 1)
			{
				fprintf(f, "  %s%u %s x%u\n", last < IMP_COUNT ? "#" : "g", last, SV_ImportTrace_Name(last), run);
			}
			else
			{
				fprintf(f, "  %s%u %s\n", last < IMP_COUNT ? "#" : "g", last, SV_ImportTrace_Name(last));
			}
		}

		last = n;
		run = 1;
	}
}
