#!/usr/bin/env python3
"""Offline verifier regression checks; synthetic bytes only."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("verifier", Path(__file__).resolve().parents[1] / "verify-dump.py")
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)

class VerifierTests(unittest.TestCase):
    def test_seven_word_header(self):
        data = bytearray(0x500)
        words = (0x80000100, 0xB1, 0, 0x80000414, 0, 0x80000420, 0x80000000)
        struct.pack_into("<7I", data, 0x400, *words)
        result = v.parse_flash_header(data)
        self.assertEqual([f["value"] for f in result["words"]], list(words))
        self.assertEqual(result["words"][-1]["name"], "app_dest_ptr")

    def test_identity_redaction(self):
        data = bytearray(0x500)
        cfg = b":CFG:MDL=X32:SN=SYNTHETIC_SECRET:MAC=AABBCCDDEEFF:OTHER=PRIVATE:\0"
        data[0x200:0x200 + len(cfg)] = cfg
        result = v.parse_cfg(data)
        self.assertEqual(result["model"], "X32")
        self.assertNotIn("SYNTHETIC_SECRET", str(result))
        self.assertNotIn("AABBCCDDEEFF", str(result))
        self.assertNotIn("PRIVATE", str(result))
        self.assertEqual(v.parse_cfg(data, True)["fields"]["SN"], "SYNTHETIC_SECRET")

if __name__ == "__main__":
    unittest.main()
