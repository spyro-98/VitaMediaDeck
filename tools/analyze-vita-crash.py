#!/usr/bin/env python3
"""Read Sony ELF32 core notes without exposing application memory/credentials.

Accepts raw or gzip-compressed .psp2dmp files. Public note layout reference:
https://github.com/TibixDev/vita-crashdump (MODULE_INFO / THREAD_REG_INFO).
This reports registers and module-relative addresses, not an unwound backtrace.
"""
import argparse
import gzip
import hashlib
import json
import struct
from pathlib import Path

MAX_BYTES = 128 * 1024 * 1024

def read_core(path):
    with path.open("rb") as stream:
        compressed = stream.read(2) == b"\x1f\x8b"
    opener = gzip.open if compressed else open
    with opener(path, "rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError("core exceeds 128 MiB analysis limit")
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("expected little-endian ELF32 core")
    if struct.unpack_from("<HH", data, 16) != (4, 40):
        raise ValueError("expected ARM ET_CORE")
    return data

def bounded(data, start, size):
    if start < 0 or size < 0 or start + size > len(data):
        raise ValueError("truncated core note")
    return data[start:start + size]

def word(data, at):
    return struct.unpack("<I", bounded(data, at, 4))[0]

def name(data, at, size):
    return bounded(data, at, size).split(b"\0", 1)[0].decode("ascii", "replace")

def notes(data, allow_partial=False):
    phoff = word(data, 28)
    size, count = struct.unpack_from("<HH", data, 42)
    if size != 32:
        raise ValueError("invalid ELF32 program header size")
    found = {}
    for i in range(count):
        h = struct.unpack("<8I", bounded(data, phoff + i * size, size))
        if h[4] and h[1] + h[4] > len(data):
            if allow_partial:
                continue
            raise ValueError("incomplete core segment; use --allow-partial for available notes")
        if h[0] != 4:
            continue
        block = bounded(data, h[1], h[4])
        at = 0
        while at + 12 <= len(block):
            ns, ds, kind = struct.unpack_from("<3I", block, at)
            at += 12
            label = name(block, at, ns)
            at += (ns + 3) & ~3
            desc = bounded(block, at, ds)
            at += (ds + 3) & ~3
            if label in ("MODULE_INFO", "THREAD_INFO", "THREAD_REG_INFO", "MEM_BLK_INFO"):
                found[label] = desc
    return found

def analyze(path, allow_partial=False):
    data = read_core(path)
    records = notes(data, allow_partial)
    modules = []
    b = records.get("MODULE_INFO", b"\0" * 8)
    at = 8
    for _ in range(word(b, 4)):
        entry = bounded(b, at, 80)
        at += 80
        segs = []
        for _ in range(word(entry, 76)):
            seg = bounded(b, at, 20)
            segs.append({"address": word(seg, 8), "bytes": word(seg, 12)})
            at += 20
        modules.append({"name": name(entry, 36, 28), "segments": segs})
        at += 16
    def resolve(address):
        for module in modules:
            for i, seg in enumerate(module["segments"]):
                if seg["address"] <= address < seg["address"] + seg["bytes"]:
                    return "%s:%d+0x%x" % (module["name"], i, address - seg["address"])
        return "0x%08x" % address
    registers = {}
    b = records.get("THREAD_REG_INFO", b"\0" * 8)
    at = 8
    for _ in range(word(b, 4)):
        entry = bounded(b, at, word(b, at))
        if len(entry) < 0x178:
            raise ValueError("short thread register record")
        registers[word(entry, 4)] = struct.unpack_from("<16I", entry, 8)
        at += len(entry)
    threads = []
    b = records.get("THREAD_INFO", b"\0" * 8)
    at = 8
    for _ in range(word(b, 4)):
        entry = bounded(b, at, word(b, at))
        if len(entry) < 0xa0:
            raise ValueError("short thread record")
        t = {"name": name(entry, 8, 32), "stop_reason": "0x%08x" % word(entry, 0x74)}
        regs = registers.get(word(entry, 4))
        if regs:
            t.update(pc="0x%08x" % regs[15], pc_module=resolve(regs[15]),
                     lr="0x%08x" % regs[14], lr_module=resolve(regs[14]), sp="0x%08x" % regs[13])
        threads.append(t)
        at += len(entry)
    blocks = []
    b = records.get("MEM_BLK_INFO", b"\0" * 8)
    # v1 records are 72 bytes; reject other shapes instead of guessing.
    if word(b, 0) == 1 and len(b) >= 8 + word(b, 4) * 72:
        for i in range(word(b, 4)):
            entry = bounded(b, 8 + i * 72, 72)
            label = name(entry, 8, 32)
            if label.startswith(("ScePaf", "SceCommonDialog")):
                blocks.append({"name": label, "type": "0x%08x" % word(entry, 40),
                               "address": "0x%08x" % word(entry, 44), "bytes": word(entry, 48)})
    phoff = word(data, 28)
    _, phcount = struct.unpack_from("<HH", data, 42)
    missing = sum(1 for i in range(phcount)
                  if (lambda h: h[4] and h[1] + h[4] > len(data))(
                      struct.unpack("<8I", bounded(data, phoff + i * 32, 32))))
    return {"file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "partial_core": bool(missing), "missing_segments": missing,
            "threads": threads, "paf_memory_blocks": blocks,
            "note": "Registers and module offsets only; no inferred backtrace or application strings."}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--allow-partial", action="store_true",
                        help="read only complete notes from an unfinished .tmp core; report missing segments")
    args = parser.parse_args()
    try:
        print(json.dumps(analyze(args.dump, args.allow_partial), indent=2))
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, "Invalid/unreadable core: %s\n" % error)
