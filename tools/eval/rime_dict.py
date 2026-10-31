"""Candidate pools from rime_ice-style .dict.yaml files."""

import math
import os

DEFAULT_DICT_FILES = ("8105", "base", "ext", "others", "tencent")


def load_pools(dict_dir, dict_files=DEFAULT_DICT_FILES, pool_size=32,
               min_pool=3):
    """Returns (pools, best_code).

    pools maps code -> [(text, log_weight)] sorted by descending weight and
    truncated to pool_size entries; codes with fewer than min_pool candidates
    are dropped. best_code maps text -> (code, log_weight) for the word's
    highest-weight code.
    """
    entries = {}
    best_code = {}
    for name in dict_files:
        path = os.path.join(dict_dir, name + ".dict.yaml")
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if (not line or line[0] in "#-\n" or
                        line.startswith("...") or line.startswith("name:")):
                    continue
                parts = line.rstrip("\n").split("\t")
                if len(parts) < 2:
                    continue
                text, code = parts[0], parts[1]
                if not text or not code or not all(c.isascii() for c in code):
                    continue
                try:
                    weight = float(parts[2]) if len(parts) > 2 and parts[2] else 0.0
                except ValueError:
                    continue
                log_weight = math.log(weight) if weight > 0 else -30.0
                bucket = entries.setdefault(code, {})
                if bucket.get(text, -1e9) < log_weight:
                    bucket[text] = log_weight
                if best_code.get(text, (None, -1e9))[1] < log_weight:
                    best_code[text] = (code, log_weight)
    pools = {
        code: sorted(bucket.items(), key=lambda kv: -kv[1])[:pool_size]
        for code, bucket in entries.items()
    }
    pools = {code: pool for code, pool in pools.items() if len(pool) >= min_pool}
    return pools, best_code
