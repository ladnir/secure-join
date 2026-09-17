#!/usr/bin/env python3
"""Reproduce Figure 2's concrete factors without running cryptography.

Run from any directory: python3 scripts/audit_figure2_constants.py
Prints JSON to stdout; does not write or modify benchmark records.
See docs/benchmarks/paper-2026/figure-2-constants.md for the derivation.
"""
import hashlib
import json
import math
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "docs/benchmarks/paper-2026"
N = 1 << 20  # Each input list, not the concatenated quicksort input.


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def option(options, name):
    return options[options.index(name) + 1]


def logstar_schedule(n, block):
    representatives = n // block
    representative_work = representatives * (representatives.bit_length() - 1) + 1
    cross_work = 2 * n * (block - 1)
    # Terminal merges reuse cross-comparison results.
    return representative_work + cross_work, representatives.bit_length() + 1


def median_schedule(n, options):
    children = [int(v) for v in option(options, "--children").split(",")]
    blocks = [int(v) for v in option(options, "--cube-blocks").split(",")]
    batches, size, work, depth = 1, n, 0, 0
    for child, block in zip(children, blocks, strict=True):
        k = size // child
        work += 2 * batches * (k * (size // block - 1) + k * (k + 1) // 2 * block)
        depth += int(size // block > 1) + 1  # Boundary, then detail comparisons.
        batches *= 2 * k
        size = child
    if option(options, "--leaf") == "allpairs":
        return work + batches * size * size, depth + 1
    assert option(options, "--leaf") == "batcher"
    return work + batches * (size * (size.bit_length() - 1) + 1), depth + size.bit_length()


def quicksort_expected_work(rows, terminal=8):
    # Independent evaluation of the recurrence used by QuickSort::plan,
    # rather than its closed-form harmonic-number expression.
    prefix = 0.0
    for size in range(rows + 1):
        expected = size * (size - 1) / 2 if size <= terminal else size - 1 + 2 * prefix / size
        prefix += expected
    return expected


def main():
    archive = json.loads((DATA / "implementation-comparison-counts.json").read_text())
    for name, expected in archive["source_sha256"].items():
        assert sha(ROOT / name) == expected, f"Archived source changed: {name}"
    for name, key in [("parameters.json", "original_parameters_sha256"),
                      ("active-median-revision.json", "active_median_manifest_sha256")]:
        assert sha(DATA / name) == archive[key], f"Archived parameters changed: {name}"
    selected = {}
    for record in archive["records"]:
        n, options = record["n"], record["selected_options"]
        if record["method"] == "logstar":
            work, depth = logstar_schedule(n, int(option(options, "--block")))
        else:
            work, depth = median_schedule(n, options)
        assert work == record["comparisons"], (record["method"], n, work, record["comparisons"])
        if n == N:
            selected[record["method"]] = (work, depth)

    # TCP endpoints describe the same execution; count each trial only once.
    # Exclude untimed round audits, as in the paper's pooled cost plots.
    trials = {}
    for line in (DATA / "measurements.jsonl").read_text().splitlines():
        r = json.loads(line)
        if (r.get("type"), r.get("method"), r.get("m"), r.get("n")) != ("benchmark", "quick", N, N):
            continue
        assert r["verified"] and r["refills"] == 0
        assert option(r["command"], "--terminal") == "8"
        assert option(r["command"], "--pivots") == "1"
        bits = r["key_bits"] + (2 * N - 1).bit_length()
        comparison_rounds = math.ceil(math.log2(bits))
        wave_rounds = comparison_rounds + 1  # Open each comparison result.
        depth, remainder = divmod(r["online_round_bound"] - 2, wave_rounds)
        assert remainder == 0
        observation = {"profile": r["profile"], "comparisons": r["comparisons"],
                       "comparison_depth": depth, "online_rounds": r["online_round_bound"]}
        if r["trial_id"] in trials:
            assert trials[r["trial_id"]] == observation
        trials[r["trial_id"]] = observation
    assert trials
    log_n = math.log2(N)
    logstar_n, value = 0, float(N)
    while value > 1:
        value = math.log2(value)
        logstar_n += 1
    exponent = math.log(2) / math.log(1.5)
    normalizers = {"logstar": (N * logstar_n, log_n),
                   "median": (N * log_n ** exponent, math.log2(log_n)),
                   "quick": (N * log_n, log_n), "batcher": (N * log_n, log_n)}
    selected["quick"] = (quicksort_expected_work(2 * N), median(r["comparison_depth"] for r in trials.values()))
    selected["batcher"] = (N * int(log_n) + 1, int(log_n) + 1)
    results = {}
    for method, (work, depth) in selected.items():
        work_scale, depth_scale = normalizers[method]
        results[method] = {"comparisons": work, "comparison_depth": depth,
                           "comparison_factor": work / work_scale, "depth_factor": depth / depth_scale,
                           "figure_cells": [f"{work / work_scale:.2f}", f"{depth / depth_scale:.2f}"]}
    observed_work = median(r["comparisons"] for r in trials.values())
    result = {"n_per_list": N, "log_star_n": logstar_n, "c": exponent,
              "verified_archived_counts": len(archive["records"]),
              "results": results, "quick_comparison_statistic": "expectation over a uniform shuffle",
              "quick_depth_statistic": "median of distinct timed executions",
              "quick_observations": list(trials.values()),
              "quick_observed_median_comparisons": observed_work,
              "quick_observed_comparison_factor": observed_work / (N * log_n),
              "input_sha256": {name: sha(DATA / name) for name in
                               ["implementation-comparison-counts.json", "measurements.jsonl",
                                "parameters.json", "active-median-revision.json"]},
              "baseline_source_sha256": {name: sha(ROOT / name) for name in
                                         ["secure-join/Sort/QuickSort.cpp", "secure-join/Sort/RootMerge.cpp"]}}
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
