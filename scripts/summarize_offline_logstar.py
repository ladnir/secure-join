#!/usr/bin/env python3
"""Validate matched preprocessing trials and write paper/report tables."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import statistics


def summarize(path):
    trials = defaultdict(list)
    hashes = set()
    for line in path.read_text().splitlines():
        r = json.loads(line)
        if not r.get('verified') or not r.get('real_crypto'):
            raise ValueError('Unverified or mock execution')
        hashes.add(r['executable_sha256'])
        trials[r['trial_id']].append(r)
    if len(hashes) != 1:
        raise ValueError('Mixed executable hashes')
    groups = defaultdict(list)
    for records in trials.values():
        r = records[0]
        local = r['profile'] == 'local'
        if sorted(x['party'] for x in records) != ([-1] if local else [0, 1]):
            raise ValueError('Duplicate or incomplete trial')
        method = 'Batcher' if r['base_case'] >= r['padded_n'] else 'Pi-logstar'
        for peer in records:
            for field in ('n', 'key_bits', 'base_case', 'block_override', 'batch_size',
                          'concurrency', 'public_seed', 'profile', 'repeat',
                          'online_gmw_ands_padded', 'online_round_bound'):
                if peer[field] != r[field]:
                    raise ValueError('Peer configuration mismatch: ' + field)
        row = dict(repeat=r['repeat'],
                   rss_gib=max(x['peak_rss_kib'] for x in records) / 2**20,
                   swap_kib=max(x['peak_sampled_swap_kib'] for x in records),
                   ands=r['online_gmw_ands_padded'], rounds=r['online_round_bound'])
        for phase in ('offline', 'online'):
            row[phase + '_s'] = max(x[phase + '_ms'] for x in records) / 1000
            row[phase + '_mib'] = (r[phase + '_total_sent_bytes'] if local else
                sum(x[f"{phase}_p{x['party']}_sent_bytes"] for x in records)) / 2**20
        row['total_s'] = row['offline_s'] + row['online_s']
        groups[r['profile'], r['n'], method, r['batch_size']].append(row)
    result = []
    for (profile, n, method, batch), rows in sorted(groups.items()):
        if sorted(r['repeat'] for r in rows) != [0, 1, 2]:
            raise ValueError('Require three distinct repetitions in every group')
        out = dict(profile=profile, n=n, method=method, batch=batch, trials=len(rows))
        for metric in ('offline_s', 'online_s', 'total_s'):
            out[metric] = statistics.median(r[metric] for r in rows)
            out[metric + '_min'] = min(r[metric] for r in rows)
            out[metric + '_max'] = max(r[metric] for r in rows)
        for metric in ('offline_mib', 'online_mib', 'ands', 'rounds'):
            values = {r[metric] for r in rows}
            if len(values) != 1:
                raise ValueError('Variable deterministic count: ' + metric)
            out[metric] = values.pop()
        out['rss_gib'] = max(r['rss_gib'] for r in rows)
        out['swap_kib'] = max(r['swap_kib'] for r in rows)
        result.append(out)
    for row in result:
        peers = [r for r in result if r['profile'] == row['profile'] and r['n'] == row['n']
                 and r['method'] == row['method']]
        if {r['batch'] for r in peers} not in ({2**20, 2**22}, {2**19, 2**20}):
            raise ValueError('Incomplete batch comparison')
        for metric in ('ands', 'rounds', 'online_mib'):
            if len({r[metric] for r in peers}) != 1:
                raise ValueError('Batch size changed the online protocol: ' + metric)
        if not any(r['profile'] == row['profile'] and r['n'] == row['n'] and
                   r['batch'] == row['batch'] and r['method'] != row['method'] for r in result):
            raise ValueError('Missing matched comparator')
    return dict(executable_sha256=hashes.pop(),
                input_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                raw_records=sum(map(len, trials.values())), protocol_executions=len(trials), rows=result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--out', type=Path, required=True, help='Output basename')
    args = parser.parse_args()
    data = summarize(args.input)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.with_suffix('.json').write_text(json.dumps(data, indent=2) + '\n')
    md = ['| Setting | Protocol | Batch | Offline MiB | Offline s (range) | Total s | Peak RSS GiB |',
          '|---|---|---:|---:|---:|---:|---:|']
    tex = [r'\begin{tabular}{llrrrr}', r'\hline',
           r'Setting & Protocol & Batch & Offline MiB & Offline s & Total s \\', r'\hline']
    for r in data['rows']:
        batch = r['batch'].bit_length() - 1
        md.append(f"| {r['profile']} | {r['method']} | 2^{batch} | {r['offline_mib']:.3f} | "
                  f"{r['offline_s']:.3f} ({r['offline_s_min']:.3f}--{r['offline_s_max']:.3f}) | "
                  f"{r['total_s']:.3f} | {r['rss_gib']:.3f} |")
        method = r'\ourprotocol' if r['method'] == 'Pi-logstar' else 'Batcher'
        tex.append(f"{r['profile'].upper()} & {method} & $2^{{{batch}}}$ & "
                   f"{r['offline_mib']:.2f} & {r['offline_s']:.2f} & {r['total_s']:.2f} " + r'\\')
    tex += [r'\hline', r'\end{tabular}']
    args.out.with_suffix('.md').write_text('\n'.join(md) + '\n')
    args.out.with_suffix('.tex').write_text('\n'.join(tex) + '\n')
    by_key = {(r['profile'], r['method'], r['batch']): r for r in data['rows']}
    if len({r['n'] for r in data['rows']}) == 1 and {'local', 'wan'} <= {r['profile'] for r in data['rows']}:
        local, large = (by_key['local', 'Pi-logstar', b] for b in (2**20, 2**22))
        wan, wan_large = (by_key['wan', 'Pi-logstar', b] for b in (2**20, 2**22))
        b, b_large = (by_key['local', 'Batcher', size] for size in (2**20, 2**22))
        reduction = 100 * (1 - large['offline_mib'] / local['offline_mib'])
        b_reduction = 100 * (1 - b_large['offline_mib'] / b['offline_mib'])
        findings = (
            f"Increasing the batch size reduces \\ourprotocol's preprocessing communication by {reduction:.1f}\\%.\n"
            f"However, its median preprocessing time increases from {local['offline_s']:.2f} to {large['offline_s']:.2f}\\,s locally\n"
            f"and from {wan['offline_s']:.2f} to {wan_large['offline_s']:.2f}\\,s on this WAN.\n"
            f"Batcher saves {b_reduction:.1f}\\% of its preprocessing bytes but also takes longer in both settings.\n"
            "The larger batches require more peak memory.\n"
            "Online byte counts and dependency bounds are identical across the two batch sizes.\n")
        if all(r['swap_kib'] == 0 for r in data['rows']):
            findings += 'No process swap was observed in the periodic samples.\n'
        findings += "These measurements do not support using $2^{22}$-entry batches to improve elapsed time in these settings.\n"
        args.out.with_name(args.out.name + '-findings.tex').write_text(findings)
    if {r['batch'] for r in data['rows']} == {2**19, 2**20} and {r['profile'] for r in data['rows']} == {'local'}:
        small, base = (by_key['local', 'Pi-logstar', b] for b in (2**19, 2**20))
        b_small, b_base = (by_key['local', 'Batcher', b] for b in (2**19, 2**20))
        findings = (
            "A separate local follow-up compares $2^{19}$-entry and $2^{20}$-entry batches,\n"
            "again with three fresh executions per protocol and configuration.\n"
            "We selected the additional setting after a one-run pilot of $2^{18}$-entry and $2^{19}$-entry batches.\n"
            f"For \\ourprotocol, the median preprocessing times are {small['offline_s']:.2f}\\,s and {base['offline_s']:.2f}\\,s, respectively,\n"
            f"with observed ranges {small['offline_s_min']:.2f}--{small['offline_s_max']:.2f}\\,s and {base['offline_s_min']:.2f}--{base['offline_s_max']:.2f}\\,s.\n"
            f"The smaller batch sends {small['offline_mib']:.2f}\\,MiB instead of {base['offline_mib']:.2f}\\,MiB offline.\n"
            f"Batcher's corresponding medians are {b_small['offline_s']:.2f}\\,s and {b_base['offline_s']:.2f}\\,s.\n"
        )
        if max(small['offline_s_min'], base['offline_s_min']) <= min(small['offline_s_max'], base['offline_s_max']):
            findings += "The observed ranges overlap, so the median difference alone does not establish a consistent speedup.\n"
        findings += "The main online results retain their original $2^{20}$-entry batch setting.\n"
        args.out.with_name(args.out.name + '-findings.tex').write_text(findings)
    print(json.dumps(data, indent=2))


if __name__ == '__main__':
    main()
