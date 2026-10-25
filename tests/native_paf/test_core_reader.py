"""Synthetic-only ELF fixtures; no console memory or firmware is distributed."""
import gzip
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("core_reader", Path(__file__).resolve().parents[2] / "tools/analyze-vita-crash.py")
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)

def core_fixture():
    header = bytearray(52)
    header[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<HH", header, 16, 4, 40)
    struct.pack_into("<I", header, 28, 52)
    struct.pack_into("<HH", header, 42, 32, 1)
    label = b"THREAD_INFO\0"
    desc = struct.pack("<II", 18, 1) + bytearray(200)
    desc = bytearray(desc)
    struct.pack_into("<II", desc, 8, 200, 123)
    desc[16:22] = b"sample"
    struct.pack_into("<I", desc, 8 + 0x74, 0x30003)
    note = struct.pack("<III", len(label), len(desc), 0x1003) + label + desc
    ph = struct.pack("<8I", 4, 84, 0, 0, len(note), 0, 0, 4)
    return bytes(header) + ph + note

class CoreReaderTests(unittest.TestCase):
    def test_raw_and_gzip_notes(self):
        with tempfile.TemporaryDirectory() as tmp:
            for compressed in (False, True):
                path = Path(tmp) / "synthetic.psp2dmp"
                raw = core_fixture()
                path.write_bytes(gzip.compress(raw) if compressed else raw)
                result = reader.analyze(path)
                self.assertEqual(result["threads"], [{"name": "sample", "stop_reason": "0x00030003"}])
                self.assertEqual(result["paf_memory_blocks"], [])
    def test_truncated_note(self):
        with self.assertRaises(ValueError):
            reader.notes(core_fixture()[:-1])
    def test_invalid_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "invalid"
            for data in (b"", b"not a core file", core_fixture()[:20]):
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    reader.read_core(path)
    def test_explicit_partial_opt_in(self):
        raw = core_fixture()
        header = bytearray(raw[:52])
        struct.pack_into("<HH", header, 42, 32, 2)
        note = raw[84:]
        first = struct.pack("<8I", 4, 116, 0, 0, len(note), 0, 0, 4)
        missing = struct.pack("<8I", 1, 116 + len(note), 0x81000000, 0, 4096, 4096, 0, 4)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "unfinished.psp2dmp.tmp"
            path.write_bytes(gzip.compress(bytes(header) + first + missing + note))
            with self.assertRaises(ValueError):
                reader.analyze(path)
            result = reader.analyze(path, allow_partial=True)
            self.assertTrue(result["partial_core"])
            self.assertEqual(result["missing_segments"], 1)
            self.assertEqual(result["threads"][0]["name"], "sample")
    def test_invalid_architecture(self):
        with tempfile.TemporaryDirectory() as tmp:
            raw = bytearray(core_fixture())
            struct.pack_into("<H", raw, 18, 3)
            path = Path(tmp) / "x86"
            path.write_bytes(raw)
            with self.assertRaises(ValueError):
                reader.read_core(path)

if __name__ == "__main__":
    unittest.main()
