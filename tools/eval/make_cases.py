#!/usr/bin/env python3
"""Generate (context, code, target) ranking cases from Chinese prose in this repo.

Each case asks: when typing the word's best pinyin code after this sentence
prefix, should the word be the top candidate? Documents are shuffled with a
fixed seed and split in half by path, so dev and test never share a source
document. Only pure pinyin codes are emitted because rime_eval types a-z keys.

The committed dev.tsv/test.tsv under tools/eval/cases/ were produced by this
script; regenerating against the same docs tree, dict set and seed reproduces
them.
"""

import argparse
import glob
import hashlib
import json
import os
import re

import numpy as np

from rime_dict import DEFAULT_DICT_FILES, load_pools

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CN = re.compile(r"[\u4e00-\u9fff]")
SENT = re.compile(r"[^。！？；\n]+[。！？；]?")
PINYIN_CODE = re.compile(r"^[a-z]+( [a-z]+)*$")


def collect_docs(docs_root, min_sentence_hanzi):
    docs = []
    for path in glob.glob(os.path.join(docs_root, "**", "*.md"),
                          recursive=True):
        if "/build" in path or "/.git" in path:
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        text = re.sub(r"```.*?```", "", text, flags=re.S)
        sentences = [s.strip() for s in SENT.findall(text)]
        sentences = [s for s in sentences
                     if len(CN.findall(s)) >= min_sentence_hanzi]
        if sentences:
            docs.append((path, sentences))
    docs.sort()
    return docs


def make_samples(sentences, pools, best_code, limit, min_context_hanzi):
    samples = []
    for sentence in sentences:
        i = 0
        while i < len(sentence) - 1:
            hit = None
            for n in (4, 3, 2):
                word = sentence[i:i + n]
                if (len(word) == n and
                        all("\u4e00" <= ch <= "\u9fff" for ch in word) and
                        word in best_code):
                    hit = (word, n)
                    break
            if not hit:
                i += 1
                continue
            word, n = hit
            code = best_code[word][0]
            if code in pools and len(CN.findall(sentence[:i])) >= min_context_hanzi:
                samples.append((sentence[:i], code, word))
                if len(samples) >= limit:
                    return samples
            i += n
    return samples


def write_cases(path, cases):
    with open(path, "w", encoding="utf-8") as f:
        for context, code, target in cases:
            f.write(f"{context}\t{code}\t{target}\n")


def main():
    parser = argparse.ArgumentParser(
        description="Generate ranking cases for rime_eval.")
    parser.add_argument("--dict-dir", required=True,
                        help="directory with rime_ice-style .dict.yaml files")
    parser.add_argument("--docs-root", default=REPO_ROOT,
                        help="root of the markdown corpus (default: repo root)")
    parser.add_argument("--out-dir", default=os.path.join(REPO_ROOT, "build",
                                                          "eval"),
                        help="where to write dev.tsv/test.tsv")
    parser.add_argument("--dict-files", default=",".join(DEFAULT_DICT_FILES),
                        help="comma-separated dict names without .dict.yaml")
    parser.add_argument("--pool-size", type=int, default=32)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--max-dev", type=int, default=1500)
    parser.add_argument("--max-test", type=int, default=1500)
    parser.add_argument("--min-sentence-hanzi", type=int, default=12)
    parser.add_argument("--min-context-hanzi", type=int, default=3)
    args = parser.parse_args()

    dict_files = tuple(args.dict_files.split(","))
    pools, best_code = load_pools(args.dict_dir, dict_files, args.pool_size)
    print(f"dict: {len(pools)} codes, {len(best_code)} words")

    docs = collect_docs(os.path.abspath(args.docs_root),
                        args.min_sentence_hanzi)
    docs.sort()
    np.random.default_rng(args.seed).shuffle(docs)
    print(f"docs: {len(docs)}, "
          f"sentences: {sum(len(s) for _, s in docs)}")

    half = len(docs) // 2
    dev = make_samples([s for _, sl in docs[:half] for s in sl], pools,
                       best_code, args.max_dev, args.min_context_hanzi)
    test = make_samples([s for _, sl in docs[half:] for s in sl], pools,
                        best_code, args.max_test, args.min_context_hanzi)
    dev = [c for c in dev if PINYIN_CODE.match(c[1])]
    test = [c for c in test if PINYIN_CODE.match(c[1])]
    print(f"cases: dev {len(dev)}, test {len(test)}")

    os.makedirs(args.out_dir, exist_ok=True)
    write_cases(os.path.join(args.out_dir, "dev.tsv"), dev)
    write_cases(os.path.join(args.out_dir, "test.tsv"), test)
    manifest = {
        "seed": args.seed,
        "pool_size": args.pool_size,
        "dict_dir": os.path.abspath(args.dict_dir),
        "dict_files": list(dict_files),
        "docs_root": os.path.abspath(args.docs_root),
        "docs": len(docs),
        "doc_fingerprint": hashlib.sha256(
            "\n".join(path for path, _ in docs).encode()).hexdigest(),
        "dev": len(dev),
        "test": len(test),
    }
    with open(os.path.join(args.out_dir, "manifest.json"), "w",
              encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print(f"wrote {args.out_dir}")


if __name__ == "__main__":
    main()
