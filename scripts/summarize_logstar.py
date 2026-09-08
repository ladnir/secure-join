#!/usr/bin/env python3
"""Summarize verified JSONL trials without double counting TCP traffic."""
import argparse
from collections import defaultdict
import json
import pathlib
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    trials = defaultdict(list)
    binaries = set()
    for path in args.inputs:
        for i, line in enumerate(path.read_text(encoding="utf-8").splitlines()):
            if not line.strip():
                continue
            r = json.loads(line)
            if r.get("type") != "benchmark":
                continue
            if not r.get("verified") or not r.get("real_crypto"):
                raise ValueError("Refusing to mix unverified or mocked results")
            binaries.add(r.get("binary_sha256", "unrecorded"))
            trials[r.get("trial_id", f"{path}:{i}")].append(r)
    if len(binaries) > 1:
        raise ValueError("Different executable builds must be summarized separately")
    groups = defaultdict(list)
    for entries in trials.values():
        first = entries[0]
        local = first["party"] == -1
        if local and len(entries) != 1:
            raise ValueError("Duplicate local trial records")
        if not local and sorted(e["party"] for e in entries) != [0, 1]:
            raise ValueError("Each TCP trial must contain exactly both parties")
        dims = ("n", "key_bits", "base_case", "block_override", "batch_size", "concurrency", "pattern")
        if any(any(e[k] != first[k] for k in dims) for e in entries):
            raise ValueError("Mismatched TCP trial configuration")
        key = (first.get("profile", first["transport"]), *(first[k] for k in dims))
        def volume(phase):
            if local:
                return first[f"{phase}_total_sent_bytes"]
            return sum(e[f"{phase}_p{e['party']}_sent_bytes"] for e in entries)
        groups[key].append({
            "offline": volume("offline"), "online": volume("online"),
            "offline_ms": max(e["offline_ms"] for e in entries),
            "online_ms": max(e["online_ms"] for e in entries),
            "rounds": first["online_round_bound"], "expanded": first["expanded_rows"],
        })
    print("Times are medians across fresh real-crypto trials; TCP uses the slower party.")
    print("Bytes sum sent bytes from both parties once; MiB = 2^20 bytes. Rounds are the online dependency-depth upper bound.\n")
    print("| Profile | n/list | Key bits | Base | Block | OT batch | Cor. concurrency | Pattern | Trials | Expanded rows | Offline MiB | Online MiB | Offline ms | Online ms | Online rounds ≤ |")
    print("|---|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|")
    for key, entries in sorted(groups.items()):
        median = lambda field: statistics.median(e[field] for e in entries)
        values = [*key, len(entries), int(median("expanded")),
                  f'{median("offline") / 2**20:.3f}', f'{median("online") / 2**20:.3f}',
                  f'{median("offline_ms"):.2f}', f'{median("online_ms"):.2f}', int(median("rounds"))]
        print("| " + " | ".join(map(str, values)) + " |")


if __name__ == "__main__":
    main()
