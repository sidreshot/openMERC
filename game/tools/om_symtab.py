#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# om_symtab.py --msvc MAP | --lld MAP  MODULE
#
# Writes the game module's symbol table into the module itself, so a savegame can name the callbacks and static
# tables its pointers point at and a later build can find them again (game/src/save/save_symtab.cpp).
# The linker map gives every function and static object with its address; the table holds name and offset from the
# image base, sorted by offset, in the block the module reserves for it (om_save_symtab, found by its placeholder).
# A name defined at different addresses by different object files (static functions of the same name) is written as
# "object!name" for each of them.
import re
import struct
import sys
from collections import defaultdict

PLACEHOLDER = b"OMSYMTAB:PLACEHOLDER:7f3a91c2"
SIZE = 1024 * 1024


def msvc_symbols(path):
    """(name, rva, object) from an MSVC /MAP: the publics and the static symbols"""
    base = None
    out = []
    rx = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8,16})\s+(?:[fi]\s+)*(\S+)\s*$")
    for line in open(path, encoding="latin-1"):
        if base is None:
            m = re.search(r"Preferred load address is ([0-9a-fA-F]+)", line)
            if m:
                base = int(m.group(1), 16)
            continue
        m = rx.match(line)
        if not m or m.group(1) == "0000":
            continue
        addr = int(m.group(4), 16)
        if addr < base:
            continue
        out.append((m.group(3), addr - base, m.group(5)))
    return out


def lld_symbols(path):
    """(name, offset, object) from an LLD -Map: symbol lines follow the input section line of their object"""
    # columns VMA LMA Size Align, then the text indented by 0 (output section), 8 (input section "obj.o:(.sec)") or
    # 16 (symbol, demangled - it may contain spaces); "$x" / "$d" are AArch64 mapping symbols
    out = []
    obj = "?"
    rx = re.compile(r"^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+\d+ (\s*)(\S.*?)\s*$")
    for line in open(path, encoding="latin-1"):
        m = rx.match(line)
        if not m:
            continue
        indent, text = len(m.group(2)), m.group(3)
        if indent < 12:
            if ":(" in text:
                obj = text.split(":(")[0].replace("\\", "/").split("/")[-1]
            continue
        if text.startswith("$") or text.startswith("."):
            continue
        out.append((text, int(m.group(1), 16), obj))
    return out


def build(symbols):
    addrs = defaultdict(set)
    for name, off, obj in symbols:
        addrs[name].add(off)
    rows = set()
    for name, off, obj in symbols:
        key = name if len(addrs[name]) == 1 else obj + "!" + name
        rows.add((off, key))
    rows = sorted(rows)
    strings = bytearray()
    entries = bytearray()
    for off, key in rows:
        entries += struct.pack("<II", off, len(strings))
        strings += key.encode("latin-1") + b"\0"
    blob = b"OMSYMTB1" + struct.pack("<II", len(rows), len(strings)) + bytes(entries) + bytes(strings)
    return blob, len(rows)


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("--msvc", "--lld"):
        sys.exit(__doc__ or "usage: om_symtab.py --msvc|--lld MAP MODULE")
    syms = msvc_symbols(sys.argv[2]) if sys.argv[1] == "--msvc" else lld_symbols(sys.argv[2])
    blob, n = build(syms)
    if len(blob) > SIZE:
        sys.exit("om_symtab: table of %d bytes does not fit the %d reserved" % (len(blob), SIZE))
    data = bytearray(open(sys.argv[3], "rb").read())
    at = data.find(PLACEHOLDER)
    if at < 0:
        if data.find(b"OMSYMTB1") >= 0:
            # already patched (a relink rewrites the file, so this is a second run on the same output)
            at = data.find(b"OMSYMTB1")
        else:
            sys.exit("om_symtab: placeholder not found in " + sys.argv[3])
    elif data.find(PLACEHOLDER, at + 1) >= 0:
        sys.exit("om_symtab: placeholder found twice")
    data[at:at + SIZE] = blob + bytes(SIZE - len(blob))
    open(sys.argv[3], "wb").write(data)
    print("om_symtab: %d symbols, %d bytes into %s" % (n, len(blob), sys.argv[3]))


if __name__ == "__main__":
    main()
