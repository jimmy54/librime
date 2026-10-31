#!/usr/bin/env python3
"""Compare two rime_eval outputs: metrics plus per-case top-1 help/hurt."""

import argparse


def load_run(path):
    ranks, top = {}, {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line[0] == "#":
                continue
            parts = line.split("\t")
            if len(parts) != 3:
                continue
            index = int(parts[0])
            ranks[index] = int(parts[1])
            top[index] = parts[2].split("|")[0] if parts[2] else None
    return ranks, top


def metrics(ranks):
    n = len(ranks)
    found = sum(rank >= 0 for rank in ranks.values())
    top1 = sum(rank == 0 for rank in ranks.values()) / n if n else 0.0
    top3 = sum(0 <= rank < 3 for rank in ranks.values()) / n if n else 0.0
    mrr = sum(1.0 / (rank + 1) for rank in ranks.values() if rank >= 0) / n \
        if n else 0.0
    return n, found, top1, top3, mrr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--label-a", default="A")
    parser.add_argument("--label-b", default="B")
    args = parser.parse_args()

    ranks_a, top_a = load_run(args.baseline)
    ranks_b, top_b = load_run(args.candidate)
    common = sorted(set(ranks_a) & set(ranks_b))
    if len(common) != len(ranks_a) or len(common) != len(ranks_b):
        print(f"warning: case sets differ "
              f"(A {len(ranks_a)}, B {len(ranks_b)}, common {len(common)})")

    print(f"{'run':<10} {'cases':>5} {'found':>5} {'top1':>7} {'top3':>7} "
          f"{'mrr':>7}")
    stats = {}
    for label, ranks in ((args.label_a, ranks_a), (args.label_b, ranks_b)):
        stats[label] = metrics(ranks)
        n, found, top1, top3, mrr = stats[label]
        print(f"{label:<10} {n:5d} {found:5d} {top1 * 100:6.2f}% "
              f"{top3 * 100:6.2f}% {mrr:7.4f}")

    churn = helped = hurt = 0
    for index in common:
        if top_a[index] != top_b[index]:
            churn += 1
            helped += ranks_a[index] != 0 and ranks_b[index] == 0
            hurt += ranks_a[index] == 0 and ranks_b[index] != 0
    n = len(common)
    delta = (stats[args.label_b][2] - stats[args.label_a][2]) * 100
    print(f"{args.label_a} -> {args.label_b}: top1 {delta:+.2f}pp, "
          f"top-1 changed {churn} ({churn / n * 100:.1f}%), "
          f"help {helped}, hurt {hurt}, net {helped - hurt:+d}")


if __name__ == "__main__":
    main()
