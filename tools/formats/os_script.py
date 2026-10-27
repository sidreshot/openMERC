#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-140
"""Reader / disassembler for compiled DesignerScript files (.os, SPEC-140).

An .os file is: int32 version (= 3), then one byte stream of instructions.
The first instructions are declarations (entity fields, then variables); the
rest is code for a stack machine. There is no other header and no section
table; the code ends at end of file. Jump operands are absolute file offsets.

usage:
  os_script.py dis <file.os> [--sp <dir>] [--raw]   print a listing (statement form;
                                                    --raw = one line per instruction)
  os_script.py validate <dir> [--sp <dir>]          parse + check every .os under <dir>
  os_script.py stats <dir>                          opcode / operand inventory
  os_script.py dump <dir> <outdir> [--sp <dir>]     write a .dsl listing per .os file

--sp <dir>: directory with .sp string packages (strip/*.sp); PRINT string ids
are then shown with their text.
"""
import os
import re
import struct
import sys
from collections import Counter, namedtuple

VERSION = 3

# value types used by declarations and PUSH
T_INT, T_FLOAT, T_VECTOR, T_ENTITY, T_STRING = 0, 1, 2, 3, 4
TYPE_NAMES = {0: "int", 1: "float", 2: "vector", 3: "entity", 4: "string"}

# PUSH kinds (byte after opcode 0x11)
P_INT, P_FLOAT, P_VECTOR, P_ENTITY, P_STRING, P_VAR, P_FIELD, P_FUNC = range(8)

# PUSH_FUNC function ids: (name, number of stack arguments or None = special)
FUNCS = {
    0: ("targetname", 1),      # entity whose "targetname" equals the string argument
    3: ("scripttarget", 1),    # entity whose "scripttarget" equals the string argument
    4: ("fn4", 0),             # entity, no arguments (UNKNOWN: activator?)
    5: ("spawn", None),        # extra byte n; 2n string arguments (value, key) pairs
    7: ("player", 0),          # the player entity (hypothesis, see SPEC-140)
    8: ("fn8", 2),             # number from two ints (UNKNOWN: random range?)
    9: ("fn9", 1),             # entity from one int (UNKNOWN: player by index?)
}

COND_OPS = {0: "==", 1: "<", 2: "<=", 3: ">", 4: ">=", 5: "!="}

DECL_OPS = {0: "global", 1: "global=", 2: "local", 3: "local=", 4: "param", 5: "param=", 6: "field"}

# opcode -> (mnemonic, immediate operand format)
#   ''  none, 'b' byte, 'i' int32, 'P' push operand, 'D' declaration
OPS = {
    0x00: ("DECL_GLOBAL", "D"), 0x01: ("DECL_GLOBAL_INIT", "D"), 0x02: ("DECL_LOCAL", "D"),
    0x03: ("DECL_LOCAL_INIT", "D"), 0x04: ("DECL_PARAM", "D"), 0x05: ("DECL_PARAM_INIT", "D"),
    0x06: ("DECL_FIELD", "D"),
    0x07: ("ASSIGN", ""), 0x08: ("ADD", ""), 0x09: ("SUB", ""), 0x0A: ("MUL", ""), 0x0B: ("DIV", ""),
    0x0C: ("ADD_ASSIGN", ""), 0x0D: ("SUB_ASSIGN", ""),
    0x10: ("GOTO", "i"), 0x11: ("PUSH", "P"), 0x13: ("IF", "bi"), 0x14: ("EXIT", ""),
    0x15: ("SUSPEND", ""), 0x16: ("WAIT", ""), 0x17: ("WAIT_SIGNALS", "b"), 0x18: ("WAIT_SIGNALS_ALT", "b"),
    0x19: ("MOVE", "b"), 0x1B: ("USE", ""), 0x1C: ("ROTATE", "b"), 0x1D: ("PRINT", "b"),
    0x1E: ("SOUND", "b"), 0x1F: ("HOLD", "b"), 0x20: ("UNHOLD", "b"), 0x21: ("AI", "i"),
    0x23: ("PRECACHE_SOUND", ""), 0x25: ("OP_25", ""), 0x27: ("REMOVE", ""), 0x28: ("HELI", "b"),
    0x29: ("ROF", "b"), 0x2A: ("MUSIC", ""), 0x2B: ("CVAR", ""), 0x2C: ("SOUND_GLOBAL", ""),
    0x2D: ("VEHICLE", "b"), 0x2F: ("PRECACHE_ROF", ""), 0x30: ("STRING_PACKAGE", ""),
    0x31: ("CONSOLE", ""), 0x32: ("ROF_RELEASE", ""), 0x33: ("WATCH", ""), 0x35: ("OP_35", ""),
}
EXPR_OPS = {0x08: "+", 0x09: "-", 0x0A: "*", 0x0B: "/"}
COMPOUND_OPS = {0x0C: "+=", 0x0D: "-="}

Instr = namedtuple("Instr", "offset op args size")       # args: tuple of immediates
Decl = namedtuple("Decl", "offset kind name type index init")


class OSError_(ValueError):
    pass


def _cstr(d, p):
    e = d.find(b"\0", p)
    if e < 0:
        raise OSError_("unterminated string at 0x%x" % p)
    return d[p:e].decode("latin-1"), e + 1


def _value(d, p, t):
    """Read a typed immediate value of type t (declaration init / PUSH constant)."""
    if t == T_INT or t == T_ENTITY:
        return struct.unpack_from("<i", d, p)[0], p + 4
    if t == T_FLOAT:
        return struct.unpack_from("<f", d, p)[0], p + 4
    if t == T_VECTOR:
        return struct.unpack_from("<3f", d, p), p + 12
    if t == T_STRING:
        return _cstr(d, p)
    raise OSError_("bad value type %d at 0x%x" % (t, p))


# ---- optional-argument flag schemes (SPEC-140 §Semantics) -------------------
def _bits(flags, table):
    return [name for bit, name in table if flags & bit]


MOVE_ARGS = [(0x01, "time"), (0x02, "speed"), (0x04, "signal")]          # 0x08: no argument
CMD_ARGS = [(0x01, "ent"), (0x02, "vec"), (0x04, "num"), (0x08, "signal")]  # HELI / VEHICLE
AI_ARGS = [(0x40, "f"), (0x04, "vec"), (0x01, "i1"), (0x08, "signal"), (0x80, "ent"),
           (0x200, "i200"), (0x400, "i400")]                               # 0x20, 0x100: no argument
SOUND_ARGS = [(0x02, "f"), (0x08, "i")]                                    # then the file name (0x01)


def arg_names(op, imm):
    """Names of the stack arguments consumed by a statement opcode (in push order)."""
    f = imm[0] if imm else 0
    if op == 0x07:
        return ["value", "target"]
    if op in COMPOUND_OPS:
        return ["value", "target"]
    if op == 0x13:
        return ["left", "right"]
    if op in (0x10, 0x14, 0x15):
        return []
    if op == 0x16:
        return ["seconds"]
    if op in (0x17, 0x18):
        return ["signal"] * (f & 0x7F)
    if op in (0x19, 0x1C):
        return ["ent", "vec"] + _bits(f, MOVE_ARGS)
    if op in (0x1B, 0x27, 0x35):
        return ["ent"]
    if op == 0x1D:
        return ["text"]
    if op == 0x1E:
        return ["ent"] + _bits(f, SOUND_ARGS) + ["file"]
    if op in (0x1F, 0x20):
        return [] if f & 0x02 else ["ent"]
    if op == 0x21:
        return ["ent", "cmd"] + _bits(f, AI_ARGS)
    if op in (0x28, 0x2D):
        return ["ent", "cmd"] + _bits(f, CMD_ARGS)
    if op == 0x29:
        return ["ent", "file"] + (["signal"] if f & 0x01 else [])
    if op == 0x25:
        return ["ent", "vec"]
    if op == 0x2B:
        return ["name", "value"]
    if op in (0x23, 0x2A, 0x2C, 0x2F, 0x30, 0x31, 0x32):
        return ["arg"]
    raise OSError_("no argument scheme for opcode 0x%02x" % op)


class Script:
    def __init__(self, data):
        if len(data) < 5:
            raise OSError_("file too short")
        self.data = data
        self.version = struct.unpack_from("<i", data, 0)[0]
        if self.version != VERSION:
            raise OSError_("version %d != %d" % (self.version, VERSION))
        self.fields = []      # Decl, index = field id
        self.vars = []        # Decl, index = variable id (globals, locals, params share one space)
        self.code = []        # Instr
        self.code_start = None
        self._decode()

    def _decode(self):
        d = self.data
        p = 4
        cond_pending = False
        while p < len(d):
            start = p
            op = d[p]
            p += 1
            if cond_pending and op <= 6:
                # tail of a WATCH condition: comparison byte + jump offset
                (off,) = struct.unpack_from("<i", d, p)
                p += 4
                self.code.append(Instr(start, "COND", (op, off), p - start))
                cond_pending = False
                continue
            if op not in OPS:
                raise OSError_("unknown opcode 0x%02x at 0x%x" % (op, start))
            fmt = OPS[op][1]
            if fmt == "D":
                if self.code:
                    raise OSError_("declaration after code at 0x%x" % start)
                name, p = _cstr(d, p)
                t = d[p]
                (idx,) = struct.unpack_from("<i", d, p + 1)
                p += 5
                init = None
                if op in (1, 3, 5):
                    init, p = _value(d, p, t)
                decl = Decl(start, op, name, t, idx, init)
                if op == 6:
                    if idx != len(self.fields):
                        raise OSError_("field index %d out of sequence" % idx)
                    self.fields.append(decl)
                else:
                    if idx != len(self.vars):
                        raise OSError_("variable index %d out of sequence" % idx)
                    self.vars.append(decl)
                continue
            if self.code_start is None:
                self.code_start = start
            args = ()
            if fmt == "b":
                args = (d[p],)
                p += 1
            elif fmt == "i":
                args = struct.unpack_from("<i", d, p)
                p += 4
            elif fmt == "bi":
                args = (d[p],) + struct.unpack_from("<i", d, p + 1)
                p += 5
            elif fmt == "P":
                kind = d[p]
                p += 1
                if kind == P_FUNC:
                    fn = d[p]
                    p += 1
                    if fn not in FUNCS:
                        raise OSError_("unknown function %d at 0x%x" % (fn, start))
                    if fn == 5:
                        args = (kind, fn, d[p])
                        p += 1
                    else:
                        args = (kind, fn)
                elif kind == P_VAR:
                    args = (kind,) + struct.unpack_from("<i", d, p)
                    p += 4
                elif kind == P_FIELD:
                    args = (kind,) + struct.unpack_from("<2i", d, p)
                    p += 8
                elif kind in (P_INT, P_FLOAT, P_VECTOR, P_ENTITY, P_STRING):
                    v, p = _value(d, p, kind)
                    args = (kind, v)
                else:
                    raise OSError_("bad push kind %d at 0x%x" % (kind, start))
            if op == 0x33:
                cond_pending = True
            if p > len(d):
                raise OSError_("instruction at 0x%x runs past end of file" % start)
            self.code.append(Instr(start, op, args, p - start))
        if cond_pending:
            raise OSError_("WATCH without condition tail")
        if p != len(d):
            raise OSError_("trailing bytes")
        if self.code_start is None:
            self.code_start = len(d)

    # ---- checks ---------------------------------------------------------------
    def validate(self):
        """Return a list of problems (empty list = consistent with SPEC-140)."""
        probs = []
        offs = {i.offset for i in self.code} | {len(self.data)}
        nv, nf = len(self.vars), len(self.fields)
        stack = []
        in_watch = False
        for ins in self.code:
            op, a = ins.op, ins.args
            if op == 0x11:
                kind = a[0]
                if kind == P_VAR and not 0 <= a[1] < nv:
                    probs.append("0x%x: variable %d out of range" % (ins.offset, a[1]))
                if kind == P_FIELD and (not 0 <= a[1] < nv or not 0 <= a[2] < nf):
                    probs.append("0x%x: field ref %r out of range" % (ins.offset, a[1:]))
                if kind == P_FUNC:
                    fn = a[1]
                    n = 2 * a[2] if fn == 5 else FUNCS[fn][1]
                    if len(stack) < n:
                        probs.append("0x%x: function %d needs %d args, stack %d" % (ins.offset, fn, n, len(stack)))
                    del stack[len(stack) - n:]
                stack.append(ins)
                continue
            if op in EXPR_OPS:
                if len(stack) < 2:
                    probs.append("0x%x: %s needs 2 operands" % (ins.offset, OPS[op][0]))
                del stack[-2:]
                stack.append(ins)
                continue
            if op == 0x33:
                if stack:
                    probs.append("0x%x: WATCH with non-empty stack" % ins.offset)
                in_watch = True
                continue
            if op == "COND":
                if len(stack) != 2:
                    probs.append("0x%x: WATCH condition has %d operands" % (ins.offset, len(stack)))
                if a[0] not in COND_OPS:
                    probs.append("0x%x: comparison %d" % (ins.offset, a[0]))
                if a[1] not in offs or a[1] < self.code_start:
                    probs.append("0x%x: WATCH target 0x%x not an instruction" % (ins.offset, a[1]))
                stack = []
                in_watch = False
                continue
            if op in (0x10, 0x13):
                tgt = a[-1]
                if tgt not in offs or tgt < self.code_start:
                    probs.append("0x%x: jump target 0x%x not an instruction" % (ins.offset, tgt))
            if op == 0x13 and a[0] not in COND_OPS:
                probs.append("0x%x: comparison %d" % (ins.offset, a[0]))
            want = len(arg_names(op, a))
            if len(stack) != want:
                probs.append("0x%x: %s expects %d stack args, has %d" % (ins.offset, OPS[op][0], want, len(stack)))
            stack = []
        if stack:
            probs.append("values left on stack at end")
        if not self.code or self.code[-1].op != 0x14:
            probs.append("code does not end with EXIT")
        return probs

    def entity_bindings(self, parms=None):
        """{var index: (lookup kind, name)} for variables assigned once from an entity lookup.

        Recognised: v = targetname("x") / scripttarget("x") / player() / fn4(); parameters are
        bound to the script_runner keys parm1..parmN in declaration order when `parms` is given.
        """
        out = {}
        params = [v for v in self.vars if v.kind in (4, 5)]
        for k, v in enumerate(params):
            if parms and ("parm%d" % (k + 1)) in parms:
                out[v.index] = ("param", parms["parm%d" % (k + 1)])
        c = self.code
        for i in range(len(c) - 2):
            a, b, s = c[i], c[i + 1], c[i + 2]
            if a.op == 0x11 and a.args[0] == P_FUNC and b.op == 0x11 and b.args[0] == P_VAR and s.op == 0x07:
                fn = a.args[1]
                if fn in (0, 3) and i > 0 and c[i - 1].op == 0x11 and c[i - 1].args[0] == P_STRING:
                    out[b.args[1]] = (FUNCS[fn][0], c[i - 1].args[1])
                elif fn in (4, 7):
                    out[b.args[1]] = (FUNCS[fn][0], None)
        return out

    def calls(self, op):
        """Yield (instruction, [argument push instructions]) for every statement with opcode `op`."""
        start = 0
        for i, ins in enumerate(self.code):
            if ins.op == 0x11 or ins.op in EXPR_OPS or ins.op == 0x33:
                continue
            if ins.op == op:
                yield ins, self.code[start:i]
            start = i + 1

    # ---- listing --------------------------------------------------------------
    def var_name(self, i):
        return self.vars[i].name if 0 <= i < len(self.vars) else "var%d" % i

    def field_name(self, i):
        return self.fields[i].name if 0 <= i < len(self.fields) else "field%d" % i

    def jump_targets(self):
        t = set()
        for ins in self.code:
            if ins.op in (0x10, 0x13) or ins.op == "COND":
                t.add(ins.args[-1])
        return t

    def listing(self, strings=None, raw=False, show_fields=False):
        out = []
        out.append("; version %d, %d fields, %d variables, code 0x%x-0x%x (%d instructions)"
                   % (self.version, len(self.fields), len(self.vars), self.code_start, len(self.data), len(self.code)))
        if show_fields:
            for f in self.fields:
                out.append("field   %-7s %-16s ; id %d" % (TYPE_NAMES.get(f.type, "?%d" % f.type), f.name, f.index))
        else:
            out.append("; fields: standard table (%s)" % ", ".join(f.name for f in self.fields))
        for v in self.vars:
            init = "" if v.init is None else " = %s" % fmt_value(v.type, v.init)
            out.append("%-7s %-7s %s%s" % (DECL_OPS[v.kind].rstrip("="), TYPE_NAMES.get(v.type, "?%d" % v.type), v.name, init))
        out.append("")
        targets = self.jump_targets()
        if raw:
            for ins in self.code:
                if ins.offset in targets:
                    out.append("L_%04x:" % ins.offset)
                out.append("  %04x  %s" % (ins.offset, self.raw_text(ins)))
            return "\n".join(out) + "\n"
        stack = []
        stmt_start = None
        watch = None
        for ins in self.code:
            if ins.offset in targets:
                if stack:
                    out.append("  ; (label inside an expression)")
                out.append("L_%04x:" % ins.offset)
            if stmt_start is None:
                stmt_start = ins.offset
            op, a = ins.op, ins.args
            if op == 0x11:
                stack.append(self.push_text(a, stack))
                continue
            if op in EXPR_OPS:
                r, l = stack.pop(), stack.pop()
                # the second-pushed value is the left operand (see SPEC-140)
                stack.append("(%s %s %s)" % (r, EXPR_OPS[op], l))
                continue
            if op == 0x33:
                watch = ins.offset
                continue
            text = self.stmt_text(ins, stack, strings)
            out.append("  %04x  %s" % (stmt_start, text))
            stack = []
            stmt_start = None
        return "\n".join(out) + "\n"

    def push_text(self, a, stack):
        kind = a[0]
        if kind == P_VAR:
            return self.var_name(a[1])
        if kind == P_FIELD:
            return "%s.%s" % (self.var_name(a[1]), self.field_name(a[2]))
        if kind == P_FUNC:
            fn = a[1]
            name = FUNCS[fn][0]
            n = 2 * a[2] if fn == 5 else FUNCS[fn][1]
            args = stack[len(stack) - n:] if n else []
            del stack[len(stack) - n:]
            if fn == 5:
                pairs = ["%s=%s" % (args[i + 1], args[i]) for i in range(0, len(args), 2)]
                return "spawn(%s)" % ", ".join(pairs)
            return "%s(%s)" % (name, ", ".join(args))
        return fmt_value(kind, a[1])

    def stmt_text(self, ins, stack, strings):
        op, a = ins.op, ins.args
        if op == "COND":
            return "watch (%s %s %s) goto L_%04x" % (stack[0], COND_OPS.get(a[0], "?%d" % a[0]), stack[1], a[1])
        if op == 0x07:
            return "%s = %s" % (stack[1], stack[0])
        if op in COMPOUND_OPS:
            return "%s %s %s" % (stack[1], COMPOUND_OPS[op], stack[0])
        if op == 0x10:
            return "goto L_%04x" % a[0]
        if op == 0x13:
            return "if not (%s %s %s) goto L_%04x" % (stack[0], COND_OPS.get(a[0], "?%d" % a[0]), stack[1], a[1])
        if op == 0x14:
            return "exit"
        if op == 0x15:
            return "suspend"
        names = arg_names(op, a)
        mn = OPS[op][0].lower()
        parts = []
        for n, v in zip(names, stack):
            parts.append(v if n in ("ent", "cmd", "vec", "file", "arg", "seconds", "text", "name", "value", "signal")
                         and n != "signal" else "%s=%s" % (n, v))
        if op in (0x17, 0x18):
            parts = list(stack)
        extra = ""
        if op == 0x1D and strings is not None and stack and re.fullmatch(r"-?\d+", stack[0]):
            extra = "   ; " + strings.describe(int(stack[0]))
        if op == 0x1D and stack and re.fullmatch(r"-?\d+", stack[0]):
            parts = ["0x%04x" % int(stack[0])]
        flags = ""
        if OPS[op][1] in ("b", "i") and op not in (0x10,):
            flags = " [flags 0x%x]" % a[0]
        return "%s(%s)%s%s" % (mn, ", ".join(parts), flags, extra)

    def raw_text(self, ins):
        op, a = ins.op, ins.args
        if op == "COND":
            return "  cond %s -> 0x%04x" % (COND_OPS.get(a[0], a[0]), a[1])
        mn = OPS[op][0]
        if op == 0x11:
            kind = a[0]
            if kind == P_VAR:
                return "PUSH var %d (%s)" % (a[1], self.var_name(a[1]))
            if kind == P_FIELD:
                return "PUSH field %d.%d (%s.%s)" % (a[1], a[2], self.var_name(a[1]), self.field_name(a[2]))
            if kind == P_FUNC:
                return "PUSH func %d%s (%s)" % (a[1], " n=%d" % a[2] if a[1] == 5 else "", FUNCS[a[1]][0])
            return "PUSH %s %s" % (TYPE_NAMES[kind], fmt_value(kind, a[1]))
        if op == 0x13:
            return "IF %s -> 0x%04x (when false)" % (COND_OPS.get(a[0], a[0]), a[1])
        if op == 0x10:
            return "GOTO 0x%04x" % a[0]
        if a:
            return "%s 0x%x" % (mn, a[0])
        return mn


def fmt_value(kind, v):
    if kind == T_FLOAT:
        return "%g" % v
    if kind == T_VECTOR:
        return "(%s)" % " ".join("%g" % c for c in v)
    if kind == T_STRING:
        return '"%s"' % v.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return str(v)


# ---- string packages (.sp), only what PRINT needs -----------------------------
class StringPackages:
    """Minimal reader for text string packages: ID, REFERENCE, INDEX blocks."""

    def __init__(self, directory):
        self.pk = {}
        for dp, _, fn in os.walk(directory):
            for f in fn:
                if f.lower().endswith(".sp"):
                    self._load(os.path.join(dp, f))

    def _load(self, path):
        txt = open(path, "rb").read().decode("latin-1")
        m = re.search(r"^ID\s+(\d+)", txt, re.M)
        r = re.search(r"^REFERENCE\s+(\S+)", txt, re.M)
        if not m:
            return
        entries = {}
        for blk in re.finditer(r"INDEX\s+(\d+)\s*\{(.*?)\}", txt, re.S):
            ref = re.search(r"REFERENCE\s+(\S+)", blk.group(2))
            en = re.search(r'TEXT_ENGLISH\s+"((?:[^"\\]|\\.)*)"', blk.group(2))
            entries[int(blk.group(1))] = (ref.group(1) if ref else "", en.group(1) if en else "")
        self.pk[int(m.group(1))] = (r.group(1) if r else os.path.basename(path), entries)

    def lookup(self, sid):
        pkg = self.pk.get((sid >> 8) & 0xFF)
        if not pkg:
            return None
        e = pkg[1].get(sid & 0xFF)
        return (pkg[0], e[0], e[1]) if e else (pkg[0], None, None)

    def describe(self, sid):
        r = self.lookup(sid)
        if r is None:
            return "string package %d not found" % ((sid >> 8) & 0xFF)
        if r[1] is None:
            return "%s index %d not found" % (r[0], sid & 0xFF)
        return '%s/%s "%s"' % (r[0], r[1], r[2])


def load(path):
    with open(path, "rb") as f:
        return Script(f.read())


def iter_files(root):
    for dp, _, fn in os.walk(root):
        for f in sorted(fn):
            if f.lower().endswith(".os"):
                yield os.path.join(dp, f)


def stats(root):
    ops = Counter()
    pushes = Counter()
    funcs = Counter()
    sizes = Counter()
    decl = Counter()
    n = 0
    for path in iter_files(root):
        s = load(path)
        n += 1
        for v in s.vars:
            decl[(DECL_OPS[v.kind], TYPE_NAMES[v.type])] += 1
        for ins in s.code:
            key = ins.op if ins.op == "COND" else OPS[ins.op][0]
            ops[key] += 1
            sizes[(key, ins.size)] += 1
            if ins.op == 0x11:
                pushes[ins.args[0]] += 1
                if ins.args[0] == P_FUNC:
                    funcs[FUNCS[ins.args[1]][0]] += 1
    print("%d files" % n)
    print("declarations:", dict(decl))
    print("opcodes:")
    for k, c in ops.most_common():
        sz = sorted((s, m) for (kk, s), m in sizes.items() if kk == k)
        print("  %-18s %6d   sizes %s" % (k, c, sz if len(sz) <= 6 else "%d..%d" % (sz[0][0], sz[-1][0])))
    print("push kinds:", {["int", "float", "vector", "entity", "string", "var", "field", "func"][k]: c
                          for k, c in sorted(pushes.items())})
    print("functions:", dict(funcs))
    # command ids of AI (0x21), HELI (0x28), VEHICLE (0x2D): second stack argument when constant
    for op in (0x21, 0x28, 0x2D):
        ids = {}
        for path in iter_files(root):
            s = load(path)
            for ins, args in s.calls(op):
                if len(args) > 1 and args[1].op == 0x11 and args[1].args[0] == P_INT:
                    e = ids.setdefault(args[1].args[1], Counter())
                    e[ins.args[0]] += 1
        print("%s command ids (%d distinct): %s" % (OPS[op][0], len(ids), " ".join(
            "%d:%d[%s]" % (k, sum(v.values()), ",".join("%x" % f for f in sorted(v))) for k, v in sorted(ids.items()))))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    sp_dir = None
    raw = "--raw" in argv
    argv = [a for a in argv if a != "--raw"]
    if "--sp" in argv:
        i = argv.index("--sp")
        sp_dir = argv[i + 1]
        del argv[i:i + 2]
    strings = StringPackages(sp_dir) if sp_dir else None
    cmd = argv[0]
    if cmd == "dis":
        sys.stdout.write(load(argv[1]).listing(strings, raw=raw))
    elif cmd == "validate":
        ok = bad = 0
        tot = Counter()
        unresolved = 0
        for path in iter_files(argv[1]):
            try:
                s = load(path)
                probs = s.validate()
            except (OSError_, struct.error, IndexError) as e:
                probs = [str(e)]
                s = None
            if strings is not None and s is not None:
                for i, ins in enumerate(s.code):
                    if ins.op == 0x1D and s.code[i - 1].op == 0x11 and s.code[i - 1].args[0] == P_INT:
                        r = strings.lookup(s.code[i - 1].args[1])
                        if r is None or r[1] is None:
                            unresolved += 1
                            probs.append("PRINT id 0x%x not in string packages" % s.code[i - 1].args[1])
            if probs:
                bad += 1
                print("FAIL %s: %s" % (path, "; ".join(probs[:4])))
            else:
                ok += 1
                tot["instructions"] += len(s.code)
                tot["variables"] += len(s.vars)
                tot["bytes"] += len(s.data)
        print("parsed %d ok, %d failed; totals %s" % (ok, bad, dict(tot)))
        return 0 if bad == 0 else 1
    elif cmd == "stats":
        stats(argv[1])
    elif cmd == "dump":
        root, outdir = argv[1], argv[2]
        n = 0
        for path in iter_files(root):
            rel = os.path.relpath(path, root)
            dst = os.path.join(outdir, os.path.splitext(rel)[0] + ".dsl")
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            s = load(path)
            with open(dst, "w", encoding="utf-8", newline="\n") as f:
                f.write("; %s\n" % rel.replace("\\", "/"))
                f.write(s.listing(strings))
            n += 1
        print("wrote %d listings to %s" % (n, outdir))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
