#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-200 (netchan framing), SPEC-201/202/203 (messages), SPEC-004 (svc_sound_info) - test harness
"""UDP relay between an original client and an original server that scripts the sound backend's inputs.

The client connects to --listen; datagrams go to --server and back. Server-to-client game packets are parsed
with tools/net/sofnet.py and changed like demoedit.py changes a demo:

  --music T:V,...        player-state music byte = V in every svc_frame from T on (SPEC-202 field 0x00800000)
  --ambient T:V,...      general ambient-set field (0x01000000, short)
  --info T:MASK:V[:V..]  append svc_sound_info (4) to the first game packet at or after T
  --cs N=TEXT            replace configstring N wherever it is sent (13 = music set)
  --sound T:IDX:VOL:ATTN:X:Y:Z
                         append svc_sound (10, SPEC-201 10) at T: sound slot IDX (CS_SOUNDS + IDX), volume 0..1,
                         attenuation 0..4, entity 0 channel 0, explicit position X Y Z (world units); X given as
                         @DX:DY:DZ = relative to the player's view (last player-state origin + view offset seen)
  --entity T0:T1:NUM:EFFECTS:SOUND:SDATA:X:Y:Z
                         add entity NUM (unused by the server, e.g. 1000) to every frame from T0 to T1 (SPEC-202 2):
                         origin, effects (0x100 = the sound field is an ambient point-set index), sound and sound-data
                         byte; X as @DX:DY:DZ = relative to the view. For 1 s after T1 the frames carry a REMOVE for it
                         (the client carries entities of its reference frame over)
  --stuff T:COMMAND      append svc_stufftext (13) with COMMAND and a newline at T: console commands for the client at known
                         server times (the client's own script must not wait after `connect`, because stuffed
                         text is appended behind whatever is still in its command buffer)

T is in seconds of server frames after the first svc_frame of the connection (frame numbers, 0.1 s each).
The client's connect request is changed to carry userinfo `cl_violence 0` (SPEC-200 connect check: the
server accepts when (sv_violence AND cl_violence) = cl_violence), because on this PC both games run with forced
parental locks (cl_violence 63, sv_violence 31) and would refuse each other; this changes nothing the sound
backend sees. Every datagram and every change is logged as JSON lines to --out (t = seconds since the relay started).
Packets that would grow beyond 1400 bytes are forwarded unchanged (logged); svc_sound_info then waits for the
next packet. Out-of-band packets pass unchanged. Stops after --seconds or when the file <out>.stop appears.
"""
import argparse
import json
import os
import re
import selectors
import socket
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import demoedit  # noqa: E402
sofnet = demoedit.sofnet

MAXLEN = 1400


def parse_addr(s):
    host, _, port = s.rpartition(":")
    return (host or "127.0.0.1", int(port))


class Editor:
    def __init__(self, a):
        self.music = demoedit.timeline(a.music, int)
        self.amb = demoedit.timeline(a.ambient, int)
        self.infos = []
        for s in a.info:
            p = s.split(":")
            self.infos.append((float(p[0]), int(p[1], 0), [int(x, 0) for x in p[2:]]))
        for s_ in a.stuff:
            t, cmd = s_.split(":", 1)
            self.infos.append((float(t), None, cmd))
        for s_ in a.sound:
            p = s_.split(":")
            rel = p[4].startswith("@")
            xyz = [float(p[4].lstrip("@")), float(p[5]), float(p[6])]
            self.infos.append((float(p[0]), "sound", [int(p[1]), float(p[2]), float(p[3])] + xyz + [rel]))
        self.origin = [0.0, 0.0, 0.0]
        self.viewofs = [0.0, 0.0, 0.0]
        self.ents = []
        for s_ in a.entity:
            p = s_.split(":")
            rel = p[6].startswith("@")
            self.ents.append((float(p[0]), float(p[1]), int(p[2]), int(p[3], 0), int(p[4], 0), int(p[5], 0),
                              [float(p[6].lstrip("@")), float(p[7]), float(p[8])], rel))
        self.ent_logged = set()
        self.infos.sort(key=lambda x: x[0])
        self.cs = {}
        for s in a.cs:
            k, v = s.split("=", 1)
            self.cs[int(k)] = v
        self.reset()

    def reset(self):
        self.sess = sofnet.Session()
        self.first = None
        self.pending = list(self.infos)
        self.last_t = None

    def entity_records(self, t, log, seq):
        """packet-entity records (SPEC-202 2.1/2.2) for the --entity specs active at t, sorted by number"""
        recs = {}
        for t0, t1, num, eff, snd, sdata, xyz, rel in self.ents:
            if t0 <= t < t1:
                pos = [o + v + d for o, v, d in zip(self.origin, self.viewofs, xyz)] if rel else list(xyz)
                hdr = 0x1 | 0x2 | 0x20 | 0x200000 | 0x1000000  # origin x y z, sound, 16-bit number
                hdr |= 0x10000 if eff < 256 else 0x4000          # effects 8-bit / 16-bit
                if snd >= 256:
                    hdr |= 0x400000
                body = struct.pack("<h", num)
                body += bytes([eff]) if eff < 256 else struct.pack("<h", eff)
                body += struct.pack("<hhh", *[int(v * 8) for v in pos])
                body += bytes([snd & 0xFF, sdata & 0xFF])
                recs[num] = (hdr, body)
                key = (num, t0)
                if key not in self.ent_logged:
                    self.ent_logged.add(key)
                    log({"event": "entity", "seq": seq, "t": round(t, 1), "number": num, "effects": eff,
                         "sound": snd, "sdata": sdata, "pos": pos})
            elif t1 <= t < t1 + 1.0 and num not in recs:
                recs[num] = (0x800 | 0x1000000, struct.pack("<h", num))  # REMOVE
        out = b""
        for num in sorted(recs):
            hdr, body = recs[num]
            if hdr & 0xFF000000:
                hdr |= 0x808080
            elif hdr & 0xFF0000:
                hdr |= 0x8080
            elif hdr & 0xFF00:
                hdr |= 0x80
            n = 4 if hdr & 0xFF000000 else 3 if hdr & 0xFF0000 else 2 if hdr & 0xFF00 else 1
            out += struct.pack("<I", hdr)[:n] + body
        return out

    def edit(self, data, seq, log):
        payload = bytearray(data[8:])
        m = sofnet.Msg(bytes(payload))
        ctx = {"t": seq, "seq": seq}
        try:
            self.sess.parse_svc(m, ctx)
        except Exception as ex:  # noqa: BLE001 - forward unchanged
            log({"event": "parse_error", "seq": seq, "error": "%s: %s" % (type(ex).__name__, ex)})
            return data
        edits = []
        tframe = None
        for name, s, e in ctx.get("spans", []):
            if name == "svc_configstring":
                idx = struct.unpack_from("<h", payload, s + 1)[0]
                if idx in self.cs:
                    edits.append((s, e, demoedit.encode_cs(idx, self.cs[idx])))
                    log({"event": "configstring", "seq": seq, "index": idx, "text": self.cs[idx]})
            elif name == "svc_frame":
                frame = struct.unpack_from("<i", payload, s + 1)[0]
                if self.first is None:
                    self.first = frame
                    log({"event": "first_frame", "seq": seq, "frame": frame})
                tframe = (frame - self.first) * 0.1
                ps = demoedit.frame_ps_start(payload, s)
                flags, pos, _ = demoedit.ps_layout(payload, ps)
                oo, on = pos["origin"]
                if on:
                    self.origin = [v / 8.0 for v in struct.unpack_from("<hhh", payload, oo)]
                vo, vn = pos["viewoffset"]
                if vn:
                    vx, vy, vz = struct.unpack_from("<bbb", payload, vo)
                    self.viewofs = [vx * 0.25, vy * 0.25, vz * 0.2 + 14.6]  # SPEC-202 4 field 16
                mo, mn = pos["music"]
                ao, an = pos["ambient"]
                orig_m = payload[mo] if mn else None
                orig_a = struct.unpack_from("<h", payload, ao)[0] if an else None
                if orig_m is not None or orig_a is not None:
                    log({"event": "server_values", "frame": frame, "t": round(tframe, 1), "music": orig_m,
                         "ambient": orig_a})
                mv = demoedit.value_at(self.music, tframe)
                av = demoedit.value_at(self.amb, tframe)
                nf = flags
                if mv is not None:
                    nf |= demoedit.MUSIC_FLAG
                    edits.append((mo, mo + mn, bytes([mv & 0xFF])))
                if av is not None:
                    nf |= demoedit.AMBIENT_FLAG
                    edits.append((ao, ao + an, struct.pack("<h", av)))
                if nf != flags:
                    edits.append((ps, ps + 4, struct.pack("<I", nf)))
                if (mv, av) != getattr(self, "_last_vals", None):
                    log({"event": "values", "frame": frame, "t": round(tframe, 1), "music": mv, "ambient": av})
                    self._last_vals = (mv, av)
                if self.ents:
                    add = self.entity_records(tframe, log, seq)
                    if add and payload[e - 2:e] == b"\0\0":
                        edits.append((e - 2, e - 2, add))
                    elif add:
                        log({"event": "entity_skip", "seq": seq, "tail": payload[e - 2:e].hex()})
            elif name == "svc_sound_info":
                log({"event": "server_sound_info", "seq": seq, "bytes": payload[s:e].hex()})
        out = bytearray(payload)
        # from the end backwards; insertions at the same offset (absent fields "where they would go", e.g. music
        # and ambient both absent) are applied in reverse order of appending, so that they keep the field order
        for _, (s, e, rep) in sorted(enumerate(edits), key=lambda x: (-x[1][0], -x[0])):
            out[s:e] = rep
        if tframe is not None:
            self.last_t = tframe
        if self.last_t is not None:
            while self.pending and self.pending[0][0] <= self.last_t + 1e-6:
                t, mask, vals = self.pending[0]
                if mask is None:
                    add = bytes([13]) + vals.encode("latin-1") + b"\n\0"
                elif mask == "sound":
                    idx, vol, attn, x, y, z, rel = vals
                    if rel:
                        x, y, z = [o + v + d for o, v, d in zip(self.origin, self.viewofs, (x, y, z))]
                        log({"event": "sound_pos", "seq": seq, "origin": self.origin, "viewofs": self.viewofs,
                             "pos": [x, y, z]})
                    flags = 0x01 | 0x02 | 0x04 | 0x08 | (0x20 if idx >= 256 else 0)
                    add = bytes([10, flags, idx & 0xFF, min(255, int(vol * 255)), min(255, int(attn * 64))])
                    add += struct.pack("<h", 0) + struct.pack("<hhh", int(x * 8), int(y * 8), int(z * 8))
                else:
                    add = bytes([4, mask & 0xFF] + [v & 0xFF for v in vals])
                if 8 + len(out) + len(add) > MAXLEN:
                    break
                out += add
                self.pending.pop(0)
                log({"event": "stufftext" if mask is None else "sound" if mask == "sound" else "sound_info",
                     "seq": seq, "t": round(self.last_t, 1),
                     "mask": mask, "values": vals})
        if 8 + len(out) > MAXLEN:
            log({"event": "too_big", "seq": seq, "len": 8 + len(out)})
            return data
        return bytes(data[:8]) + bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen", required=True)
    ap.add_argument("--server", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seconds", type=float, default=600)
    ap.add_argument("--music")
    ap.add_argument("--ambient")
    ap.add_argument("--info", action="append", default=[])
    ap.add_argument("--cs", action="append", default=[])
    ap.add_argument("--stuff", action="append", default=[])
    ap.add_argument("--sound", action="append", default=[])
    ap.add_argument("--entity", action="append", default=[])
    ap.add_argument("--raw", action="store_true", help="also log every datagram (hex)")
    a = ap.parse_args()

    ed = Editor(a)
    sel = selectors.DefaultSelector()
    lsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    lsock.bind(parse_addr(a.listen))
    lsock.setblocking(False)
    sel.register(lsock, selectors.EVENT_READ, None)
    server = parse_addr(a.server)
    upstream = {}
    t0 = time.time()
    out = open(a.out, "w", encoding="ascii", buffering=1)
    stopfile = a.out + ".stop"
    if os.path.exists(stopfile):
        os.remove(stopfile)

    def log(rec):
        rec = dict(rec)
        rec["wall"] = round(time.time(), 3)
        rec["qpc"] = round(time.perf_counter(), 4)  # same clock as proccap's packet time stamps
        rec["t_relay"] = round(time.time() - t0, 3)
        out.write(json.dumps(rec) + "\n")

    log({"event": "start", "listen": a.listen, "server": a.server, "music": a.music, "ambient": a.ambient,
         "info": a.info, "cs": a.cs})
    while time.time() - t0 < a.seconds and not os.path.exists(stopfile):
        for key, _ in sel.select(timeout=0.2):
            sock = key.fileobj
            try:
                data, addr = sock.recvfrom(65536)
            except (BlockingIOError, ConnectionResetError):
                continue
            if key.data is None:  # from a client
                up = upstream.get(addr)
                if up is None:
                    up = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    up.bind(("127.0.0.1", 0))
                    up.setblocking(False)
                    sel.register(up, selectors.EVENT_READ, addr)
                    upstream[addr] = up
                if data[:12] == b"\xff\xff\xff\xffconnect ":
                    new = re.sub(rb"\\cl_violence\\[0-9]+", rb"\\cl_violence\\0", data)
                    if new != data:
                        log({"event": "connect_cl_violence_0"})
                    data = new
                if a.raw:
                    log({"dir": "c2s", "len": len(data), "hex": data.hex()})
                up.sendto(data, server)
            else:  # from the server, for client key.data
                if data[:4] == b"\xff\xff\xff\xff":
                    if data[4:].startswith(b"client_connect"):
                        ed.reset()
                        log({"event": "client_connect"})
                    new = data
                elif len(data) >= 8:
                    seq = struct.unpack_from("<I", data, 0)[0] & 0x7FFFFFFF
                    new = ed.edit(data, seq, log)
                else:
                    new = data
                if a.raw:
                    log({"dir": "s2c", "len": len(new), "hex": new.hex()})
                lsock.sendto(new, key.data)
    log({"event": "stop"})
    out.close()


if __name__ == "__main__":
    main()
