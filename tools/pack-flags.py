#!/usr/bin/env python3
"""Pack original PNG bytes into one indexed resource; no image recompression.

This avoids hundreds of tiny filesystem operations during VPK installation.
The reader loads only the requested flag, never the complete resource.
"""
import argparse
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    entries = [b""] * (26 * 26)
    for path in sorted(args.source.glob("*.png")):
        code = path.stem
        if code in ("gb-eng", "gb-sct", "gb-wls", "gb-nir"):
            continue  # Catalogue IDs are ISO alpha-2; these source variants are unused.
        if len(code) != 2 or any(c < "a" or c > "z" for c in code):
            raise ValueError(f"Invalid country code: {code}")
        data = path.read_bytes()
        if not data.startswith(b"\x89PNG\r\n\x1a\n") or len(data) > 65536:
            raise ValueError(f"Invalid flag: {path}")
        entries[(ord(code[0]) - 97) * 26 + ord(code[1]) - 97] = data
    offset = 8 + len(entries) * 8
    index = bytearray(b"VMDFLG1\0")
    for data in entries:
        index.extend(struct.pack("<II", offset if data else 0, len(data)))
        offset += len(data)
    args.output.write_bytes(index + b"".join(entries))
    print(f"Packed {sum(bool(e) for e in entries)} flags: {offset} bytes, one resource")


if __name__ == "__main__":
    main()
