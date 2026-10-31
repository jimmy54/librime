#!/usr/bin/env python3
"""Reconstruct the ChaoInput CLMG v2 tokenizer vocabulary.

The model header only stores a FNV-1a hash of the vocabulary, so both the eval
harness and the packer rebuild it from the tokenizer sources: single Han
characters from dict.db plus the punctuation ranges in LmtChars.h.
"""

import os
import re
import sqlite3
import struct


def fnv1a(codepoints, vocab_size):
    h = 0xcbf29ce484222325
    chunks = [struct.pack("<Q", vocab_size)]
    chunks += [struct.pack("<I", cp) for cp in codepoints]
    for chunk in chunks:
        for byte in chunk:
            h ^= byte
            h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def build_vocab(model_pkg, source_dir, vocab_size, vocab_hash):
    with open(os.path.join(source_dir, "core", "lmt", "LmtChars.h"),
              encoding="utf-8") as f:
        header = f.read()
    ranges = [(int(lo, 16), int(hi, 16)) for lo, hi in re.findall(
        r"\{0x([0-9A-Fa-f]{4,6}),\s*0x([0-9A-Fa-f]{4,6})\}", header)]
    extra = set()
    for lo, hi in ranges:
        extra.update(range(lo, hi + 1))
    conn = sqlite3.connect(os.path.join(model_pkg, "data", "dict.db"))
    try:
        cjk = {ord(text) for (text,) in
               conn.execute("select text from dict")
               if len(text) == 1 and 0x4E00 <= ord(text) <= 0x9FFF}
    finally:
        conn.close()
    codepoints = sorted(cjk | extra)
    if (32 + len(codepoints) != vocab_size or
            fnv1a(codepoints, vocab_size) != vocab_hash):
        raise SystemExit("vocabulary does not match the model header hash")
    return {cp: 32 + i for i, cp in enumerate(codepoints)}
