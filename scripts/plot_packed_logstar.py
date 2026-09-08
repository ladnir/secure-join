#!/usr/bin/env python3
"""Verified paired measurements -> paper figures, CSV, and a Markdown table."""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
import statistics as stats
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def load(paths):
    trials = defaultdict(list)
    hashes = set()
    for path in paths:
        for line in path.read_text().splitlines():
            r = json.loads(line)
            if r.get('type') != 'benchmark':
                continue
            if not r.get('verified') or not r.get('real_crypto'):
                raise ValueError('Unverified or mocked record')
            hashes.add(r['binary_sha256'])
            trials[r['trial_id']].append(r)
    if len(hashes) != 1:
        raise ValueError('Exactly one binary hash is required')
    groups = defaultdict(list)
    for records in trials.values():
        r = records[0]
        parties = sorted(x['party'] for x in records)
        if parties not in ([-1], [0, 1]):
            raise ValueError('Duplicate local record or incomplete TCP pair')
        for peer in records:
            for field in ('n', 'key_bits', 'base_case', 'block_override', 'batch_size', 'concurrency', 'pattern', 'public_seed', 'profile', 'online_round_bound'):
                if peer[field] != r[field]:
                    raise ValueError(f'TCP public configuration mismatch: {field}')
        if r['key_bits'] != 32 or r['pattern'] != 'random' or r['concurrency'] != 2:
            raise ValueError('Figure set requires the declared 32-bit configuration')
        # The local harness snapshots both endpoints together. TCP endpoints
        # snapshot separately; charge sent bytes only at those phase boundaries.
        if parties == [-1]:
            for phase in ('online', 'offline'):
                for party in (0, 1):
                    if r[f'{phase}_p{party}_sent_bytes'] != r[f'{phase}_p{1-party}_received_bytes']:
                        raise ValueError(f'Unmatched local sent/received accounting in {phase}')
        method = 'Batcher' if r['base_case'] >= r['padded_n'] else 'Pi-logstar'
        row = dict(profile=r['profile'], n=r['n'], method=method, block=r['block_override'],
                   batch=r['batch_size'], rounds=r['online_round_bound'], ands=r['online_gmw_ands_padded'],
                   rss_gib=max(x['peak_rss_kib'] for x in records) / 2**20,
                   sampled_swap_gib=max(x['peak_sampled_swap_kib'] for x in records) / 2**20)
        for phase in ('online', 'offline'):
            row[phase + '_mib'] = (r[phase + '_total_sent_bytes'] if parties == [-1] else
                sum(x[f"{phase}_p{x['party']}_sent_bytes"] for x in records)) / 2**20
            row[phase + '_s'] = max(x[phase + '_ms'] for x in records) / 1000
        row['total_s'] = row['online_s'] + row['offline_s']
        row['total_mib'] = row['online_mib'] + row['offline_mib']
        groups[(row['profile'], row['n'], method)].append(row)
    summary = {}
    for key, rows in groups.items():
        out = {k: (stats.median(r[k] for r in rows) if isinstance(v, (int, float)) else v)
               for k, v in rows[0].items()}
        for k in ('batch', 'block', 'rounds', 'ands', 'online_mib'):
            if len({r[k] for r in rows}) != 1:
                raise ValueError(f'Mixed public schedules or online bytes in one plotted group: {key}/{k}')
        out['trials'] = len(rows)
        for metric in ('online_s', 'offline_s', 'total_s'):
            out[metric + '_min'] = min(r[metric] for r in rows)
            out[metric + '_max'] = max(r[metric] for r in rows)
        out['rss_gib'] = max(r['rss_gib'] for r in rows)
        out['sampled_swap_gib'] = max(r['sampled_swap_gib'] for r in rows)
        summary[key] = out
    for (profile, n, method), row in summary.items():
        other = summary.get((profile, n, 'Batcher' if method == 'Pi-logstar' else 'Pi-logstar'))
        if not other or row['batch'] != other['batch']:
            raise ValueError('Missing comparator or unmatched correlation batch')
        if profile != 'local' and ('local', n, method) in summary:
            local = summary['local', n, method]
            for field in ('block', 'batch', 'rounds', 'ands'):
                if row[field] != local[field]:
                    raise ValueError(f'Local/TCP schedule or communication mismatch: {n}/{method}/{field}')
            # coproto registers a previously unused session with an 8-byte
            # Header and a 16-byte ControlBlock. Local Batcher first uses the
            # root session online; TCP uses it in the excluded configuration
            # handshake. Packed preprocessing already uses the root session.
            # Keep the actual counters and accept only this exact difference.
            expected_delta = -48 if method == 'Batcher' else 0
            if (row['online_mib'] - local['online_mib']) * 2**20 != expected_delta:
                raise ValueError(f'Unexplained local/TCP framing difference: {n}/{method}')
    return summary, hashes.pop()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs', nargs='+', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    data, binary = load(args.inputs)
    args.output.mkdir(parents=True, exist_ok=True)
    all_rows = [data[k] for k in sorted(data)]
    with (args.output / 'packed-plot-data.csv').open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(all_rows[0])); w.writeheader(); w.writerows(all_rows)
    (args.output / 'packed-summary.json').write_text(json.dumps(dict(binary_sha256=binary, rows=all_rows), indent=2) + '\n')
    plt.rcParams.update({'font.family': 'serif', 'font.size': 9, 'axes.labelsize': 9,
                        'legend.fontsize': 8, 'xtick.labelsize': 8, 'ytick.labelsize': 8,
                        'axes.spines.top': False, 'axes.spines.right': False,
                        'pdf.fonttype': 42, 'ps.fonttype': 42})
    colors = {'Pi-logstar': '#1764a0', 'Batcher': '#b4511d'}
    markers = {'Pi-logstar': 'o', 'Batcher': 's'}
    labels = {'Pi-logstar': r'$\Pi$-Logstar', 'Batcher': 'Batcher'}
    sizes = sorted(n for profile, n, method in data if profile == 'local' and method == 'Pi-logstar')
    exps = np.log2(sizes).astype(int)

    def save(fig, name):
        fig.savefig(args.output / (name + '.pdf'), bbox_inches='tight')
        fig.savefig(args.output / (name + '.png'), dpi=180, bbox_inches='tight')
        plt.close(fig)

    def line(ax, metric, method, errors=False):
        rows = [data['local', n, method] for n in sizes]
        y = np.array([r[metric] for r in rows])
        err = np.array([[r[metric] - r[metric + '_min'] for r in rows],
                        [r[metric + '_max'] - r[metric] for r in rows]]) if errors else None
        ax.errorbar(exps, y, yerr=err, color=colors[method], marker=markers[method],
                    markersize=3.5, capsize=2, linewidth=1.3, label=labels[method])
        ax.set_xticks(exps[::2]); ax.set_xticks(exps, minor=True)
        ax.set_xlabel(r'$\log_2 n$ (keys per list)'); ax.grid(alpha=.2)

    fig, axs = plt.subplots(1, 2, figsize=(5.0, 2.6), layout='constrained')
    for method in colors:
        line(axs[0], 'online_mib', method)
    for ax, label in ((axs[0], 'Online communication (MiB)'),):
        ax.set_yscale('log'); ax.set_ylabel(label); ax.legend()
    ratio = [data['local', n, 'Pi-logstar']['online_mib'] / data['local', n, 'Batcher']['online_mib'] for n in sizes]
    axs[1].plot(exps, ratio, '-o', color=colors['Pi-logstar'], markersize=4)
    axs[1].axhline(1, color='#555555', linestyle='--', linewidth=1)
    axs[1].set_xticks(exps[::2]); axs[1].set_xticks(exps, minor=True)
    axs[1].set_xlabel(r'$\log_2 n$ (keys per list)')
    axs[1].set_ylabel('Online bytes / Batcher'); axs[1].grid(alpha=.2)
    if any(r < 1 for r in ratio):
        first = next(i for i, r in enumerate(ratio) if r < 1)
        axs[1].annotate('first sampled win', (exps[first], ratio[first]), xytext=(.30, .87),
                        textcoords='axes fraction', fontsize=8, ha='left', va='bottom',
                        arrowprops=dict(arrowstyle='->', color='#555555', lw=.8))
    save(fig, 'packed-communication')

    fig, axs = plt.subplots(1, 2, figsize=(5.0, 2.6), layout='constrained')
    for method in colors:
        line(axs[0], 'online_s', method, True); line(axs[1], 'offline_s', method, True)
    for ax, ylabel in zip(axs, ('Local online time (s)', 'Preprocessing time (s)')):
        ax.set_ylabel(ylabel); ax.legend()
    axs[0].set_yscale('log'); axs[1].set_yscale('log')
    save(fig, 'packed-time-depth')

    network = sorted({(profile, n) for profile, n, method in data if profile != 'local'},
                     key=lambda item: (item[1], {'lan': 0, 'wan': 1, 'slow': 2}[item[0]]))
    if network:
        fig, axs = plt.subplots(2, 1, figsize=(5.0, 4.0), layout='constrained')
        x = np.arange(len(network)); width = .36
        tick_labels = [f"{p.upper()}\n$2^{{{n.bit_length()-1}}}$" for p, n in network]
        for j, method in enumerate(colors):
            rows = [data[profile, n, method] for profile, n in network]
            for ax, metric in zip(axs, ('online_s', 'total_s')):
                ys = np.array([r[metric] for r in rows])
                err = np.array([[r[metric] - r[metric + '_min'] for r in rows],
                                [r[metric + '_max'] - r[metric] for r in rows]])
                ax.bar(x + (j - .5) * width, ys, width, yerr=err, capsize=2,
                       label=labels[method], color=colors[method])
        for ax, label in zip(axs, ('TCP online time (s)', 'TCP offline + online time (s)')):
            ax.set_xticks(x, tick_labels); ax.set_yscale('log'); ax.set_ylabel(label); ax.legend(); ax.grid(axis='y', alpha=.2)
        save(fig, 'packed-network')

    text = ['# Optimized Pi-logstar measurements', '', f'Executable SHA-256: `{binary}`.', '',
            'All rows are verified real executions. MiB = 2^20 bytes; n is per input list.', '',
            '| n/list | Protocol | m | Batch | Online MiB | Offline MiB | Online s | Offline s | Depth | Peak RSS GiB | Trials |',
            '|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for n in sizes:
        for method in colors:
            r = data['local', n, method]
            text.append(f"| {n:,} | {method} | {r['block']} | {r['batch']} | {r['online_mib']:.3f} | "
                        f"{r['offline_mib']:.3f} | {r['online_s']:.4f} | {r['offline_s']:.3f} | {r['rounds']} | "
                        f"{r['rss_gib']:.2f} | {r['trials']} |")
    text += ['', '| Profile | n/list | Protocol | Online s | Offline s | Total s | Trials |',
             '|---|---:|---|---:|---:|---:|---:|']
    for profile, n in network:
        for method in colors:
            r = data[profile, n, method]
            text.append(f"| {profile} | {n:,} | {method} | {r['online_s']:.3f} | {r['offline_s']:.3f} | {r['total_s']:.3f} | {r['trials']} |")
    (args.output / 'packed-results.md').write_text('\n'.join(text) + '\n')


if __name__ == '__main__':
    main()
