#!/usr/bin/env python3
"""Reproduce the paper's measured Pi-logstar/Batcher figures from raw JSONL.

Matplotlib and NumPy are required. Every point comes from a verified trial; no
asymptotic extrapolation or interpolated break-even size is reported.
"""
import argparse
from collections import defaultdict
import csv
import json
import math
from pathlib import Path
import statistics

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def load_trials(paths):
    trials = defaultdict(list)
    hashes = set()
    for path in paths:
        for line in path.read_text().splitlines():
            r = json.loads(line)
            if r.get("type") != "benchmark":
                continue
            if not r.get("verified") or not r.get("real_crypto"):
                raise ValueError(f"Unverified or mocked record in {path}")
            hashes.add(r["binary_sha256"])
            trials[r["trial_id"]].append(r)
    if len(hashes) != 1:
        raise ValueError("Use one executable build per figure set")
    groups = defaultdict(list)
    for records in trials.values():
        r = records[0]
        local = r["party"] == -1
        if (local and len(records) != 1) or (not local and sorted(x["party"] for x in records) != [0, 1]):
            raise ValueError("Duplicate local record or incomplete TCP pair")
        if r["key_bits"] != 32 or r["pattern"] != "random" or r["concurrency"] != 2:
            continue
        if r["base_case"] >= r["padded_n"]:
            variant = "Batcher"
        elif r["base_case"] == 16 and r["block_override"] == 8:
            variant = "Pi-logstar"
        else:
            continue
        expected_batch = 262144 if r["n"] <= 65536 else 1048576
        if r["batch_size"] != expected_batch:
            continue
        key = (r["profile"], r["n"], variant)
        entry = {"rounds": r["online_round_bound"], "batch": r["batch_size"]}
        for phase in ("offline", "online"):
            entry[phase + "_bytes"] = (r[phase + "_total_sent_bytes"] if local else
                sum(x[f"{phase}_p{x['party']}_sent_bytes"] for x in records))
            entry[phase + "_ms"] = max(x[phase + "_ms"] for x in records)
        entry["wall_ms"] = max(x["wall_ms"] for x in records)
        entry["rss_kib"] = max(x["peak_rss_kib"] for x in records)
        groups[key].append(entry)
    result = {}
    for key, rows in groups.items():
        result[key] = {field: statistics.median(r[field] for r in rows) for field in rows[0]}
        result[key]["trials"] = len(rows)
        for field in ("offline_ms", "online_ms"):
            result[key][field + "_min"] = min(r[field] for r in rows)
            result[key][field + "_max"] = max(r[field] for r in rows)
    return result, next(iter(hashes))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("inputs", nargs="+", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--paged-sizes", default="", help="Comma-separated Pi-logstar sizes with independently observed paging")
    args = ap.parse_args()
    data, binary = load_trials(args.inputs)
    args.output.mkdir(parents=True, exist_ok=True)
    paged_sizes = {int(n) for n in args.paged_sizes.split(",") if n}
    plt.rcParams.update({"font.family": "serif", "font.size": 8, "axes.labelsize": 8,
        "axes.titlesize": 8, "legend.fontsize": 7, "xtick.labelsize": 7,
        "ytick.labelsize": 7, "pdf.fonttype": 42, "ps.fonttype": 42,
        "axes.spines.top": False, "axes.spines.right": False})
    variants = ["Pi-logstar", "Batcher"]
    colors = {"Pi-logstar": "#1764a0", "Batcher": "#b4511d"}
    labels = {"Pi-logstar": r"$\Pi$-Logstar ($m=8$)", "Batcher": "Batcher's merge"}
    markers = {"Pi-logstar": "o", "Batcher": "s"}
    ns = sorted(n for profile, n, v in data if profile == "local" and v == "Pi-logstar"
                and (profile, n, "Batcher") in data)
    if not ns:
        raise ValueError("No matched local comparison points")
    exponents = [int(math.log2(n)) for n in ns]

    def xaxis(ax):
        ax.set_xticks(exponents, [rf"$2^{{{i}}}$" for i in exponents])
        ax.set_xlabel("Keys per input list, $n$")
        ax.grid(axis="y", color="0.87", linewidth=.5)

    def lines(ax, field, scale=1, log=False, ranges=False):
        for v in variants:
            rows = [data["local", n, v] for n in ns]
            y = np.array([r[field] / scale for r in rows])
            if ranges:
                lo = [r[field + "_min"] / scale for r in rows]
                hi = [r[field + "_max"] / scale for r in rows]
                ax.errorbar(exponents, y, yerr=[y - lo, hi - y], color=colors[v],
                    marker=markers[v], markersize=3.5, linewidth=1.2, capsize=2, label=labels[v])
                if v == "Pi-logstar":
                    for exponent, n, value in zip(exponents, ns, y):
                        if n in paged_sizes:
                            label_pos = (.72, .98) if field == "offline_ms" else (.88, .83)
                            ax.annotate("paging", (exponent, value), xytext=label_pos,
                                textcoords="axes fraction", ha="right", va="top", fontsize=7,
                                color=colors[v], arrowprops={"arrowstyle": "->", "linewidth": .7,
                                    "color": colors[v]})
            else:
                ax.plot(exponents, y, color=colors[v], marker=markers[v],
                    markersize=3.5, linewidth=1.2, label=labels[v])
        if log:
            ax.set_yscale("log")
        xaxis(ax)

    def save(fig, name):
        fig.savefig(args.output / (name + ".pdf"), bbox_inches="tight", pad_inches=.025,
                    metadata={"Title": name, "Subject": "Measured verified two-party trials; " + binary})
        fig.savefig(args.output / (name + ".png"), dpi=220, bbox_inches="tight", pad_inches=.025)
        plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(4.85, 2.05), layout="constrained")
    lines(axes[0], "online_bytes", 2**20, log=True)
    axes[0].set_ylabel("Online communication (MiB)")
    axes[0].legend(loc="upper left", frameon=False)
    ratios = [data["local", n, "Pi-logstar"]["online_bytes"] /
              data["local", n, "Batcher"]["online_bytes"] for n in ns]
    axes[1].plot(exponents, ratios, "o-", color=colors["Pi-logstar"], markersize=3.5, linewidth=1.2)
    axes[1].axhline(1, color="0.3", linestyle="--", linewidth=.8)
    axes[1].set_ylabel(r"Online bytes: $\Pi$-Logstar / Batcher")
    xaxis(axes[1])
    first = next((i for i, ratio in enumerate(ratios) if ratio < 1), None)
    if first is not None:
        axes[1].annotate(rf"First measured win: $2^{{{exponents[first]}}}$", xy=(exponents[first], ratios[first]),
            xytext=(.98, .72), textcoords="axes fraction", ha="right", fontsize=7,
            arrowprops={"arrowstyle": "->", "linewidth": .7})
    save(fig, "pi-logstar-communication")

    fig, axes = plt.subplots(1, 2, figsize=(4.85, 2.05), layout="constrained")
    lines(axes[0], "online_ms", log=True, ranges=True)
    axes[0].set_ylabel("Local online elapsed time (ms)")
    axes[0].legend(loc="upper left", frameon=False)
    lines(axes[1], "offline_ms", 1000, log=True, ranges=True)
    axes[1].set_ylabel("Local offline elapsed time (s)")
    save(fig, "pi-logstar-computation")

    fig, axes = plt.subplots(1, 2, figsize=(4.85, 2.05), layout="constrained")
    lines(axes[0], "offline_bytes", 2**20, log=True)
    axes[0].set_ylabel("Offline communication (MiB)")
    axes[0].legend(loc="upper left", frameon=False)
    lines(axes[1], "rounds")
    axes[1].set_ylabel("Online dependency\ndepth bound")
    save(fig, "pi-logstar-offline-rounds")

    profiles = ["lan", "wan", "slow"]
    if all((p, 4096, v) in data for p in profiles for v in variants):
        fig, axes = plt.subplots(1, 2, figsize=(4.85, 2.15), layout="constrained")
        for ax, phase in zip(axes, ["online", "offline"]):
            for idx, v in enumerate(variants):
                rows = [data[p, 4096, v] for p in profiles]
                y = np.array([r[phase + "_ms"] / 1000 for r in rows])
                lo = [r[phase + "_ms_min"] / 1000 for r in rows]
                hi = [r[phase + "_ms_max"] / 1000 for r in rows]
                ax.bar(np.arange(3) + (idx - .5) * .34, y, width=.32, color=colors[v],
                       label=labels[v], yerr=[y - lo, hi - y], capsize=2)
            ax.set_xticks(range(3), ["1 Gbit/s\n0.2 ms", "100 Mbit/s\n40 ms", "10 Mbit/s\n40 ms"])
            ax.set_xlabel("Rate per direction / configured RTT")
            ax.set_ylabel(phase.capitalize() + " elapsed time (s)")
            ax.grid(axis="y", color="0.87", linewidth=.5)
            ax.set_axisbelow(True)
        axes[0].legend(frameon=False, loc="upper left")
        save(fig, "pi-logstar-network")

    with (args.output / "pi-logstar-plot-data.csv").open("w", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=["profile", "n_per_list", "variant", *next(iter(data.values()))])
        writer.writeheader()
        for (profile, n, variant), row in sorted(data.items()):
            writer.writerow({"profile": profile, "n_per_list": n, "variant": variant, **row})
    print(json.dumps({"binary_sha256": binary, "matched_sizes": ns,
        "first_measured_communication_win": ns[first] if first is not None else None,
        "largest_communication_ratio": ratios[-1], "figures_directory": str(args.output)}))


if __name__ == "__main__":
    main()
