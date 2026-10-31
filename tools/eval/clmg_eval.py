#!/usr/bin/env python3
"""Offline evaluation of the ChaoInput CLMG v2 re-rank residual.

    score(c) = log(rime_weight[c]) + alpha * (h . mean(W_c[chars of c]))

The model is reconstructed from the shipped weights
(ChaoInput-*/data/lmt.bin): the character vocabulary is rebuilt from
ChaoInput's tokenizer ranges (core/lmt/LmtChars.h) and dict.db, then checked
against the vocabulary hash stored in the model header. With --engine-out it
also checks target ranks and pool ordering against rime_eval's context-off
output.

Only exact-code dictionary candidates are re-ranked here; segmentations that
the engine adds to its menu (other codes contributing to the same key string)
are outside this offline simulation, so the gains are a proxy until the
re-ranker runs inside the engine. The printed churn/help/hurt counts are
relative to the alpha=0 ranking.
"""

import argparse
import math
import os
import re
import sqlite3
import struct
import sys

import numpy as np

from rime_dict import DEFAULT_DICT_FILES, load_pools


def fnv1a(codepoints, vocab_size):
    h = 0xcbf29ce484222325
    chunks = [struct.pack("<Q", vocab_size)]
    chunks += [struct.pack("<I", cp) for cp in codepoints]
    for chunk in chunks:
        for byte in chunk:
            h ^= byte
            h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def layer_norm(x, gain, bias, eps=1e-5):
    mean = x.mean(-1, keepdims=True)
    var = x.var(-1, keepdims=True)
    return (x - mean) / np.sqrt(var + eps) * gain + bias


def gelu(x):
    return 0.5 * x * (1 + np.tanh(0.7978845608 * (x + 0.044715 * x ** 3)))


class Clmg:
    def __init__(self, model_pkg, source_dir, trunc=64):
        self.trunc = trunc
        lmt_path = os.path.join(model_pkg, "data", "lmt.bin")
        with open(lmt_path, "rb") as f:
            raw = f.read()
        (magic, version, num_layers, hidden, heads, ffn, vocab_size,
         max_seq) = struct.unpack_from("<4sIIIIIII", raw, 0)
        vocab_hash, obs, seed, param_count, _ = struct.unpack_from(
            "<QQQQQ", raw, 32)
        if magic != b"CLMG" or version != 2:
            raise SystemExit(f"unexpected model header: {magic!r} v{version}")
        params = np.frombuffer(raw, dtype=np.float32, count=param_count,
                               offset=72)
        offset = 0

        def take(n):
            nonlocal offset
            block = params[offset:offset + n]
            offset += n
            return block

        self.hidden = hidden
        self.heads = heads
        self.max_seq = max_seq
        self.emb = take(vocab_size * hidden).reshape(vocab_size, hidden)
        self.pos = take(max_seq * hidden).reshape(max_seq, hidden)
        self.ln_fg, self.ln_fb = take(hidden), take(hidden)
        self.layers = []
        for _ in range(num_layers):
            layer = {"ln1g": take(hidden), "ln1b": take(hidden)}
            for name in ("q", "k", "v", "o"):
                layer[name + "w"] = take(hidden * hidden).reshape(hidden, hidden)
                layer[name + "b"] = take(hidden)
            layer["ln2g"], layer["ln2b"] = take(hidden), take(hidden)
            layer["f1w"] = take(hidden * ffn).reshape(hidden, ffn)
            layer["f1b"] = take(ffn)
            layer["f2w"] = take(ffn * hidden).reshape(ffn, hidden)
            layer["f2b"] = take(hidden)
            self.layers.append(layer)
        self.head_w = take(vocab_size * hidden).reshape(vocab_size, hidden)
        if offset != param_count:
            raise SystemExit(
                f"weight layout mismatch: read {offset}, header {param_count}")
        self.vocab_size = vocab_size
        self.obs = obs
        self.cp2id = self._build_vocab(model_pkg, source_dir, vocab_size,
                                       vocab_hash)

    @staticmethod
    def _build_vocab(model_pkg, source_dir, vocab_size, vocab_hash):
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

    def encode(self, text):
        ids = []
        for ch in text:
            cp = ord(ch)
            if cp in self.cp2id:
                ids.append(self.cp2id[cp])
            elif 0x4E00 <= cp <= 0x9FFF:
                ids.append(31)
            elif "a" <= ch <= "z":
                ids.append(5 + cp - 97)
            elif "A" <= ch <= "Z":
                ids.append(5 + cp - 65)
        return ids

    def context_hidden(self, context):
        ids = [1] + self.encode(context)[-self.trunc:]
        length = len(ids)
        x = self.emb[ids] + self.pos[:length]
        head_dim = self.hidden // self.heads
        mask = np.triu(np.full((length, length), -1e30), 1)
        for layer in self.layers:
            y = layer_norm(x, layer["ln1g"], layer["ln1b"])
            q = y @ layer["qw"] + layer["qb"]
            k = y @ layer["kw"] + layer["kb"]
            v = y @ layer["vw"] + layer["vb"]
            attended = np.empty_like(x)
            for h in range(self.heads):
                sl = slice(h * head_dim, (h + 1) * head_dim)
                scores = (q[:, sl] @ k[:, sl].T) / math.sqrt(head_dim) + mask
                e = np.exp(scores - scores.max(-1, keepdims=True))
                attended[:, sl] = (e / e.sum(-1, keepdims=True)) @ v[:, sl]
            x = x + (attended @ layer["ow"] + layer["ob"])
            y = layer_norm(x, layer["ln2g"], layer["ln2b"])
            x = x + (gelu(y @ layer["f1w"] + layer["f1b"]) @ layer["f2w"] +
                     layer["f2b"])
        return layer_norm(x, self.ln_fg, self.ln_fb)[-1]

    def head_vec(self, text):
        ids = [i for i in self.encode(text) if i >= 32]
        if not ids:
            return np.zeros(self.hidden, np.float32)
        return self.head_w[ids].mean(0)


def load_cases(path):
    cases = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 3:
                cases.append(tuple(parts))
    return cases


def load_engine_out(path):
    ranks, candidates = {}, {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not line or line[0] == "#":
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 3:
                continue
            index = int(parts[0])
            ranks[index] = int(parts[1])
            candidates[index] = parts[2].split("|") if parts[2] else []
    return ranks, candidates


def check_engine_parity(cases, pools, engine_out):
    engine_ranks, engine_candidates = load_engine_out(engine_out)
    rank_mismatch = order_mismatch = covered = total = 0
    examples = []
    for index, (_, code, target) in enumerate(cases):
        if index not in engine_candidates:
            continue
        pool = pools.get(code)
        if not pool:
            continue
        texts = [text for text, _ in pool]
        position = {text: i for i, text in enumerate(texts)}
        shown = engine_candidates[index]
        total += len(shown)
        offline_rank = position.get(target, -1)
        expected = offline_rank if 0 <= offline_rank < len(shown) else -1
        if engine_ranks.get(index) != expected:
            rank_mismatch += 1
            if len(examples) < 5:
                examples.append((index, texts[:10], shown))
        last = -1
        for text in shown:
            if text not in position:
                continue
            covered += 1
            if position[text] < last:
                order_mismatch += 1
                break
            last = position[text]
    cases_seen = len(engine_candidates)
    coverage = covered / total * 100 if total else 0.0
    print(f"engine parity: target-rank mismatches {rank_mismatch}/{cases_seen}, "
          f"cases with pool-order inversion {order_mismatch}, "
          f"exact-code share {coverage:.1f}% of shown candidates")
    for index, offline, engine in examples:
        print(f"  case {index}: offline={offline} engine={engine}")


def build_rows(clmg, cases, pools):
    rows = []
    missing = skipped = 0
    for count, (context, code, target) in enumerate(cases, 1):
        pool = pools.get(code)
        if not pool:
            skipped += 1
            continue
        texts = [text for text, _ in pool]
        if target not in texts:
            missing += 1
            continue
        hidden = clmg.context_hidden(context)
        base = np.array([weight for _, weight in pool])
        residual = np.array(
            [float(np.dot(hidden, clmg.head_vec(text))) for text in texts])
        rows.append((texts.index(target), base, residual))
        if count % 500 == 0:
            print(f"  scored {count}/{len(cases)}", file=sys.stderr, flush=True)
    return rows, missing, skipped


def evaluate(rows, alphas):
    print(f"{'alpha':>5} {'top1':>7} {'top3':>7} {'mrr':>7} {'churn':>6} "
          f"{'help':>5} {'hurt':>5}")
    for alpha in alphas:
        top1 = top3 = churn = helped = hurt = 0
        mrr = 0.0
        for target_index, base, residual in rows:
            order = np.argsort(-(base + alpha * residual), kind="stable")
            rank = int(np.where(order == target_index)[0][0])
            top1 += rank == 0
            top3 += rank < 3
            mrr += 1.0 / (rank + 1)
            baseline_top = int(np.argmax(base))
            if order[0] != baseline_top:
                churn += 1
                helped += baseline_top != target_index and order[0] == target_index
                hurt += baseline_top == target_index
        n = len(rows)
        print(f"{alpha:5.2f} {top1 / n * 100:6.2f}% {top3 / n * 100:6.2f}% "
              f"{mrr / n:7.4f} {churn / n * 100:5.1f}% {helped:5d} {hurt:5d}")


def main():
    parser = argparse.ArgumentParser(
        description="Offline ChaoInput CLMG re-rank evaluation.")
    parser.add_argument("--model-pkg", required=True,
                        help="ChaoInput package dir (data/lmt.bin, data/dict.db)")
    parser.add_argument("--chaoinput-src", required=True,
                        help="ChaoInput source tree (core/lmt/LmtChars.h)")
    parser.add_argument("--dict-dir", required=True,
                        help="directory with rime_ice-style .dict.yaml files")
    parser.add_argument("--cases", required=True,
                        help="dev.tsv/test.tsv produced by make_cases.py")
    parser.add_argument("--engine-out",
                        help="rime_eval --no-contextual output to check "
                             "pool/rank parity")
    parser.add_argument("--dict-files", default=",".join(DEFAULT_DICT_FILES))
    parser.add_argument("--pool-size", type=int, default=32)
    parser.add_argument("--alpha", default="0,0.25,0.5,1.0",
                        help="comma-separated re-rank weights")
    parser.add_argument("--trunc", type=int, default=64,
                        help="context characters kept (model window is 64)")
    args = parser.parse_args()

    alphas = [float(a) for a in args.alpha.split(",")]
    pools, _ = load_pools(args.dict_dir, tuple(args.dict_files.split(",")),
                          args.pool_size)
    print(f"pools: {len(pools)} codes")
    cases = load_cases(args.cases)
    print(f"cases: {len(cases)}")
    if args.engine_out:
        check_engine_parity(cases, pools, args.engine_out)

    clmg = Clmg(args.model_pkg, args.chaoinput_src, args.trunc)
    print(f"model: V={clmg.vocab_size} H={clmg.hidden} obs={clmg.obs}")
    rows, missing, skipped = build_rows(clmg, cases, pools)
    print(f"target in pool: {len(rows)} (missing {missing}, "
          f"code not in pool {skipped})")
    evaluate(rows, alphas)


if __name__ == "__main__":
    main()
