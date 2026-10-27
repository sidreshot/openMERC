#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G6b - small assembler for DesignerScript test programs (.os, version 3), our own test data.
# Encoding of SPEC-140 (instruction stream, PUSH kinds, flags) with the operand orders of SPEC-750 (ASSIGN: value then
# target; arithmetic: right then left; IF: left then right; optional arguments in their fixed push order).
# Implements: SPEC-140 (encoding), SPEC-750 (operand order)

import struct

T_INT, T_FLOAT, T_VECTOR, T_ENTITY, T_STRING = 0, 1, 2, 3, 4
EQ, LT, LE, GT, GE, NE = 0, 1, 2, 3, 4, 5


class Asm:
    def __init__(self):
        self.b = bytearray(struct.pack("<i", 3))
        self.vars = {}
        self.fields = {}
        self.fixups = []      # (offset, label)
        self.labels = {}

    # --- raw ---
    def u8(self, v): self.b.append(v & 0xFF)
    def i32(self, v): self.b += struct.pack("<i", v)
    def f32(self, v): self.b += struct.pack("<f", v)
    def str_(self, s): self.b += s.encode("latin-1") + b"\0"
    def here(self): return len(self.b)

    def label(self, name): self.labels[name] = self.here()

    def target(self, label):
        self.fixups.append((self.here(), label))
        self.i32(0)

    def build(self):
        for at, lab in self.fixups:
            struct.pack_into("<i", self.b, at, self.labels[lab])
        return bytes(self.b)

    # --- declarations (fields first, then variables) ---
    def field(self, name, typ):
        fid = len(self.fields)
        self.u8(0x06); self.str_(name); self.u8(typ); self.i32(fid)
        self.fields[name] = fid
        return fid

    def local(self, name, typ):
        idx = len(self.vars)
        self.u8(0x02); self.str_(name); self.u8(typ); self.i32(idx)
        self.vars[name] = idx
        return idx

    # --- pushes ---
    def pint(self, v): self.u8(0x11); self.u8(0); self.i32(v)
    def pfloat(self, v): self.u8(0x11); self.u8(1); self.f32(v)
    def pvec(self, x, y, z): self.u8(0x11); self.u8(2); self.f32(x); self.f32(y); self.f32(z)
    def pstr(self, s): self.u8(0x11); self.u8(4); self.str_(s)
    def pvar(self, name): self.u8(0x11); self.u8(5); self.i32(self.vars[name])
    def pfield(self, var, field): self.u8(0x11); self.u8(6); self.i32(self.vars[var]); self.i32(self.fields[field])

    def pfn(self, fid, n=None):
        self.u8(0x11); self.u8(7); self.u8(fid)
        if fid == 5:
            self.u8(n)

    # --- statements ---
    def assign(self, push_value, push_target):
        push_value(); push_target(); self.u8(0x07)

    def add_assign(self, push_value, push_target):
        push_value(); push_target(); self.u8(0x0C)

    def goto(self, label): self.u8(0x10); self.target(label)

    def if_not(self, push_left, push_right, cmp, label):
        """continue when (left cmp right) holds, else jump to label"""
        push_left(); push_right(); self.u8(0x13); self.u8(cmp); self.target(label)

    def exit(self): self.u8(0x14)
    def suspend(self): self.u8(0x15)

    def wait(self, secs):
        self.pfloat(secs); self.u8(0x16)

    def waitsignals(self, names, clear=True):
        for n in names:
            self.pvar(n)
        self.u8(0x17); self.u8((0x80 if clear else 0) | len(names))

    def move(self, ent, vec, dur=None, speed=None, sig=None, absolute=False, rotate=False):
        fl = (0x01 if dur is not None else 0) | (0x02 if speed is not None else 0) | (0x04 if sig else 0) | \
             (0x08 if absolute else 0)
        self.pvar(ent); self.pvec(*vec)
        if dur is not None:
            self.pfloat(dur)
        if speed is not None:
            self.pfloat(speed)
        if sig:
            self.pvar(sig)
        self.u8(0x1C if rotate else 0x19); self.u8(fl)

    def use(self, ent): self.pvar(ent); self.u8(0x1B)

    def print_text(self, text):
        self.pstr(text); self.u8(0x1D); self.u8(0)

    def print_id(self, sid):
        self.pint(sid); self.u8(0x1D); self.u8(0)

    def sound(self, ent, name, volume=None, channel=None):
        fl = 0x01 | (0x02 if volume is not None else 0) | (0x08 if channel is not None else 0)
        self.pvar(ent)
        if volume is not None:
            self.pfloat(volume)
        if channel is not None:
            self.pint(channel)
        self.pstr(name); self.u8(0x1E); self.u8(fl)

    def cinematic(self, start):
        self.u8(0x1F if start else 0x20); self.u8(2)

    def precache_sound(self, name): self.pstr(name); self.u8(0x23)
    def string_package(self, name): self.pstr(name); self.u8(0x30)
    def console(self, text): self.pstr(text); self.u8(0x31)
    def music(self, n): self.pint(n); self.u8(0x2A)

    def view_yaw(self, ent, yaw):
        self.pvar(ent); self.pvec(0, yaw, 0); self.u8(0x25)

    def remove(self, ent): self.pvar(ent); self.u8(0x27)

    def rof(self, ent, name, sig=None):
        self.pvar(ent); self.pstr(name)
        if sig:
            self.pvar(sig)
        self.u8(0x29); self.u8(0x01 if sig else 0)

    def spawn(self, target_var, keys):
        """target_var := spawn(keys); keys = [(key, value)], pushed as value then key"""
        for k, v in reversed(keys):
            self.pstr(v); self.pstr(k)
        self.pfn(5, len(keys)); self.pvar(target_var); self.u8(0x07)
