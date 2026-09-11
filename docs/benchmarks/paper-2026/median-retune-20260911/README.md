# Completed Median-only revision, September 11, 2026

All 93 timed executions and 13 independent round audits are complete: every n = 2^8 through 2^20 on local, LAN, and WAN transports. The revision is active through `../active-median-revision.json`, and the actual paper, figures, tables, abstract, and introduction use the validated results. The full composite study remains 390 configurations, 930 timed executions, and 130 audits. All non-Median observations and asymmetric figures are unchanged.

## Public parameter selection

The search screens power-of-two recursion shapes up to four levels, then uses the existing compiled planner for exact cost counts of shortlisted schedules and block/leaf variants. The objective minimizes online dependency rounds subject to application payload at most max(3 times Batcher online bytes, 8 MiB), breaking ties by payload. It does not establish global optimality. The selected schedules have one or two levels; n = 2^17 uses two. To reduce recursion expansion and peak memory, the two largest sizes are restricted to one-level Batcher-leaf schedules: child/cube block 1024/32 at n = 2^19 and 2048/64 at n = 2^20. The same schedule is used on every network profile.

`parameters.json` freezes the first selection; `memory-feasible/parameters.json` separately freezes the one-level choices for the two largest sizes. Their `median_revision` metadata overrides the inherited original-study selection description for Median. Neither parameter file was mutated after its runs began. `plans.jsonl` and `frontier.json` preserve the public candidate search. The parameter-frontier diagnostic is a historical planner plot of the initial 110-round candidate, not a measured timing or the published largest-size selection.

## Accepted measurements and fairness

- `measurements.jsonl`: n = 2^8 through 2^18, 87 timed executions and 11 audits.
- `memory-feasible/measurements.jsonl`: n = 2^19 and 2^20, six timed executions and two audits.
- `validation.json`: complete-grid, public-schedule, byte-count, executable, and preservation checks.
- `nonmedian-preservation.json`: original non-Median observations and root-artifact hashes.

The executable, 32-bit keys, fresh real cryptography, stable-output verification, correlation batches of 2^20 with concurrency two, one OS thread per TCP party, and network profiles match the frozen study. Every accepted timed run has zero sampled Linux process swap, sampled every 0.5 seconds. No successful timing was selected for being unusually fast. Failed and interrupted attempts are recorded separately and excluded.

At n = 2^20, all three profiles have exactly 3,226,804,576 online bytes and a 121-round dependency bound. The independent audit measured 23 offline and 119 online synchronous waves; the paper consistently reports the implementation dependency bound. Compared with Batcher, this is 17.7 percent fewer reported online rounds and 2.03 times the online bytes. Median remains slower at this large size. At n = 2^11 it uses 47 rather than 84 rounds and has a 1.45 times WAN online speedup, with 6.53 rather than 1.33 MiB traffic.

## Memory handling

Earlier attempts were stopped preemptively by a Windows free-memory reserve, not an observed OOM crash. After the user rebooted, the observer used its explicit `--observe-only` mode and allowed normal host-memory reclamation. Local, LAN, and WAN n = 2^20 timings all completed without sampled Linux process swap.

The larger buffers in the separate round audit required paging. The first after-reboot audit completed but its result was rejected by the old blanket no-swap rule. It was repeated after allowing explicitly marked paging for untimed audits only. The accepted audit recorded a sampled swap peak of 5,159,680 KiB. Its elapsed times are excluded from every performance table and curve. Paging changes elapsed time, not transcript byte counts, synchronous message-wave counts, or verified functionality. The validator still rejects paging in timed records, even if an audit flag is incorrectly attached.

The original raw study is immutable. Environment, failure, pilot, and memory-observer records preserve the retry history; they are not substitute measurements. The temporary WSL configuration is restored during final cleanup, as recorded in `checkpoint.json` and the parent `artifact-verification.json`.

## Reproduction

Run from the code repository in the configured Ubuntu/WSL environment, using fresh output paths. Network profiles require root. The runner resumes only matching executable and parameter hashes.

```sh
sudo python3 scripts/run_paper_benchmarks.py --methods median --shapes balanced --min-exp 8 --max-exp 18 --parameters docs/benchmarks/paper-2026/median-retune-20260911/parameters.json --out out/reproduce-median-small.jsonl
sudo python3 scripts/run_paper_benchmarks.py --methods median --shapes balanced --min-exp 19 --max-exp 20 --parameters docs/benchmarks/paper-2026/median-retune-20260911/memory-feasible/parameters.json --out out/reproduce-median-large.jsonl
```

To regenerate the delivered composite study from its validated frozen records:

```sh
python3 scripts/summarize_paper_benchmarks.py --paper-study --plot-shapes balanced --out ../64c0aeaf1c1f5473b45f1e06/plots/benchmark
python3 scripts/write_paper_benchmark_section.py
```
