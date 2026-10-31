#!/usr/bin/env python3
"""Pack ChaoInput's CLMG v2 weights into librime's clmg.bin format.

The source lmt.bin already holds a flat fp32 parameter block, but its header
describes training state and its vocabulary is implicit. The packed file keeps
the parameter order unchanged and adds an explicit sorted codepoint table so
the plugin can encode text without the tokenizer sources:

    offset 0   ClmgModelHeader (80 bytes, little endian)
    offset 80  uint32 codepoints[vocab_size], ascending
    ...        zero padding to a 16-byte boundary
    ...        float32 params[param_count] (same order as lmt.bin)

Usage:
    pack_clmg.py --model-pkg <ChaoInput dir> --chaoinput-src <source tree> \
                 --output clmg.bin
"""

import argparse
import os
import struct
import sys

from clmg_vocab import build_vocab

MAGIC = b"RimeCLMG"
VERSION = 1
HEADER = struct.Struct("<8sIIIIIIIIQQII4I")  # 80 bytes
PARAMS_OFFSET_IN_LMT = 72
SOURCE_MAGIC = b"CLMG"


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def expected_param_count(num_layers, hidden, ffn, vocab_size, max_seq):
    per_layer = (2 * hidden +              # ln1 gain/bias
                 4 * (hidden * hidden + hidden) +  # q/k/v/o
                 2 * hidden +              # ln2 gain/bias
                 hidden * ffn + ffn +      # f1
                 ffn * hidden + hidden)    # f2
    return (vocab_size * hidden + max_seq * hidden +
            2 * hidden + num_layers * per_layer + vocab_size * hidden)


def main():
    parser = argparse.ArgumentParser(
        description="Pack ChaoInput CLMG v2 weights for the librime plugin.")
    parser.add_argument("--model-pkg", required=True,
                        help="ChaoInput package dir (data/lmt.bin, data/dict.db)")
    parser.add_argument("--chaoinput-src", required=True,
                        help="ChaoInput source tree (core/lmt/LmtChars.h)")
    parser.add_argument("--output", required=True, help="output clmg.bin path")
    args = parser.parse_args()

    with open(os.path.join(args.model_pkg, "data", "lmt.bin"), "rb") as f:
        raw = f.read()
    (magic, version, num_layers, hidden, heads, ffn, vocab_size,
     max_seq) = struct.unpack_from("<4sIIIIIII", raw, 0)
    vocab_hash, _, _, param_count, _ = struct.unpack_from("<QQQQQ", raw, 32)
    if magic != SOURCE_MAGIC or version != 2:
        raise SystemExit(f"unexpected lmt.bin header: {magic!r} v{version}")
    if len(raw) != PARAMS_OFFSET_IN_LMT + 4 * param_count:
        raise SystemExit("lmt.bin size does not match its param count")
    expected = expected_param_count(num_layers, hidden, ffn, vocab_size,
                                   max_seq)
    if param_count != expected:
        raise SystemExit(
            f"parameter layout mismatch: {param_count} != {expected}")

    cp2id = build_vocab(args.model_pkg, args.chaoinput_src, vocab_size,
                        vocab_hash)
    codepoints = sorted(cp2id)
    vocab_count = len(codepoints)
    vocab_offset = HEADER.size
    params_offset = align(vocab_offset + 4 * vocab_count, 16)

    header = HEADER.pack(
        MAGIC, VERSION, num_layers, hidden, heads, ffn, vocab_size, max_seq,
        vocab_count, vocab_hash, param_count, vocab_offset, params_offset,
        0, 0, 0, 0)
    with open(args.output, "wb") as f:
        f.write(header)
        f.write(struct.pack(f"<{vocab_count}I", *codepoints))
        f.write(b"\0" * (params_offset - vocab_offset - 4 * vocab_count))
        f.write(raw[PARAMS_OFFSET_IN_LMT:])

    print(f"packed {args.output}: L={num_layers} H={hidden} heads={heads} "
          f"F={ffn} V={vocab_size} seq={max_seq} "
          f"params={param_count} ({param_count * 4 / 2**20:.1f} MiB), "
          f"vocab@{vocab_offset}, params@{params_offset}", file=sys.stderr)


if __name__ == "__main__":
    main()
