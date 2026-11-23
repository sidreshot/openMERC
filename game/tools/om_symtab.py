#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# om_symtab.py --msvc MAP | --lld MAP | --ld64 MAP  MODULE
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


def ld64_symbols(path):
    """(name, offset, object) from a map of Apple's linker (-map): the "# Symbols:" table"""
    # "# Object files:" lists "[  n] path" (or "path/lib.a(member.o)"); "# Symbols:" has "0xADDRESS 0xSIZE [  n] name".
    # A dynamic library is linked at address 0, so the address is the offset from the image base. Names are the
    # linker's (C names with the leading underscore taken off, C++ names mangled); the linker's own labels and
    # literals ("literal string: ...", "non-lazy-pointer", "ltmp1", "GCC_except_table3") are not symbols of the code.
    out = []
    objs = {}
    section = ""
    rx_obj = re.compile(r"^\[\s*(\d+)\]\s+(.*?)\s*$")
    rx_sym = re.compile(r"^0x([0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+\[\s*(\d+)\]\s+(\S+)\s*$")
    for line in open(path, encoding="latin-1"):
        if line.startswith("#"):
            if not line.startswith("# Address"):  # the column titles under "# Sections:" and "# Symbols:"
                section = line[1:].strip()
            continue
        if section.startswith("Object files"):
            m = rx_obj.match(line)
            if m:
                name = m.group(2).replace("\\", "/").split("/")[-1]
                if name.endswith(")") and "(" in name:
                    name = name[name.rindex("(") + 1:-1]
                objs[m.group(1)] = name
        elif section.startswith("Symbols"):
            m = rx_sym.match(line)
            if m and m.group(3).startswith("_"):
                out.append((m.group(3)[1:], int(m.group(1), 16), objs.get(m.group(2), "?")))
    return out


def build(symbols):
    addrs = defaultdict(set)
    for name, off, obj in symbols:
        addrs[name].add(off)
    rows = set()
    for name, off, obj in symbols:
        key = name if len(addrs[name]) == 1 else "%s@%x!%s" % (obj, off, name)
        rows.add((off, key))
    rows = sorted(rows)
    strings = bytearray()
    entries = bytearray()
    for off, key in rows:
        entries += struct.pack("<II", off, len(strings))
        strings += key.encode("latin-1") + b"\0"
    blob = b"OMSYMTB1" + struct.pack("<II", len(rows), len(strings)) + bytes(entries) + bytes(strings)
    return blob, len(rows)


def nm_symbols(path):
    """(name, offset, object) from nm. GNU ld's map lists only globals, so a static callback such as
    props::train_think was never in the table and every save stored it as none. nm -a still has the locals
    of an unstripped module. The address in a shared object is the offset from the load base."""
    import subprocess
    text = subprocess.check_output(["nm", "-a", "--defined-only", path], text=True, errors="replace")
    out = []
    for line in text.splitlines():
        parts = line.split(None, 2)
        if len(parts) != 3:
            continue
        addr, typ, name = parts
        if typ not in "tTdDbBrRvVwW":
            continue
        if name.startswith(".") or name.startswith("$"):
            continue
        try:
            off = int(addr, 16)
        except ValueError:
            continue
        out.append((name, off, "nm"))
    return out


def find_slot(data):
    at = data.find(PLACEHOLDER)
    if at >= 0:
        if data.find(PLACEHOLDER, at + 1) >= 0:
            sys.exit("om_symtab: placeholder found twice")
        return at
    # a second run: the real table, not a chance "OMSYMTB1" inside instructions
    idx = 0
    while True:
        at = data.find(b"OMSYMTB1", idx)
        if at < 0:
            return -1
        if at + 16 <= len(data):
            n, sb = struct.unpack_from("<II", data, at + 8)
            if n < 200000 and sb < SIZE and 16 + n * 8 + sb <= SIZE:
                return at
        idx = at + 1


def main():
    readers = {"--msvc": msvc_symbols, "--lld": lld_symbols, "--ld64": ld64_symbols}
    if len(sys.argv) == 3 and sys.argv[1] == "--nm":
        syms = nm_symbols(sys.argv[2])
        module = sys.argv[2]
    elif len(sys.argv) == 4 and sys.argv[1] in readers:
        syms = readers[sys.argv[1]](sys.argv[2])
        module = sys.argv[3]
    else:
        sys.exit(__doc__ or "usage: om_symtab.py --nm MODULE | --msvc|--lld|--ld64 MAP MODULE")
    if not syms:
        sys.exit("om_symtab: no symbols from %s" % sys.argv[1])
    blob, n = build(syms)
    if len(blob) > SIZE:
        sys.exit("om_symtab: table of %d bytes does not fit the %d reserved" % (len(blob), SIZE))
    data = bytearray(open(module, "rb").read())
    at = find_slot(data)
    if at < 0:
        sys.exit("om_symtab: placeholder not found in " + module)
    # duplicate names at different addresses must not collapse onto one key
    data[at:at + SIZE] = blob + bytes(SIZE - len(blob))
    open(module, "wb").write(data)
    print("om_symtab: %d symbols, %d bytes into %s" % (n, len(blob), module))


if __name__ == "__main__":
    main()
