// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - private declarations of the Ghoul glue (G9). Not for other packages.
// Implements: SPEC-791 Interface 1-4
#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gg_ghoul.h"

struct GGSkinPair {
	char material[100];
	char skin[100];
};

struct GGModel {
	GGModel *next;            // creation order
	char dir[100];
	char list[100];
	char skin[100];
	char base[100];
	ghoul::IGhoulObject *obj;
	uint8_t allskins;
	uint8_t prepared;
	uint8_t avail[1024];
	GGSkinPair *pairs;        // skin pairs registered while not prepared (savegames)
	int npairs, cappairs;
	GGInst *insts;            // object instances of this model
	uint32_t id;              // AI object id (0 = not yet handed out)
	uint32_t serial;          // creation order across kinds
};

struct GGInst {
	GGInst *next;             // in the model's list
	GGModel *model;
	ghoul::IGhoulInst *inst;
	edict_t *owner;
	GhoulID seq;
	GGBolt *bolts;            // direct bolt-ons, attach order
	uint32_t id;
	uint32_t serial;
};

struct GGBolt {
	GGBolt *next;             // sibling in the parent's list
	GGInst *root;             // set for direct bolt-ons
	GGBolt *parent;           // set for bolt-ons on bolt-ons
	GhoulID pbolt, cbolt;
	GGModel *child;
	float scale;
	GhoulID seq;
	ghoul::IGhoulInst *inst;
	GGBolt *bolts;            // own bolt-ons
	uint32_t id;
	uint32_t serial;
};

extern GGModel *gg_models;
extern GGAttached *gg_attached[MAX_EDICTS];
extern uint32_t gg_serial;

ghoul::IGhoulSystem *GG_System(void);
void gg_strcpy(char *dst, const char *src, int size);
void gg_conprint(const char *fmt, ...);
void gg_devprint(const char *fmt, ...);
GGModel *gg_newrecord(const char *dir, const char *list, const char *skin, const char *base);
void gg_linkrecord(GGModel *m);
bool gg_registerobject(GGModel *m);
void gg_rememberpair(GGModel *m, const char *material, const char *skin);
void gg_linkinst(GGModel *m, GGInst *oi);
GGInst *gg_allocinst(void);
GGBolt *gg_allocbolt(void);
void gg_attachedappend(GGAttached *a, GGBolt *b);
void gg_attachedforget(GGBolt *b);

// registry of all bolt-ons (gg_save.cpp): ids, savegames, statistics
void gg_rememberbolt(GGBolt *b);
void gg_forgetbolt(GGBolt *b);
void gg_resetboltregistry(void);
int gg_boltcount(void);
void gg_resetidmap(void);
