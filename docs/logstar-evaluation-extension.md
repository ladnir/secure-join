# Evaluation extension through 2^20 keys per party

All protocol runs use the unchanged executable SHA-256
`8bd169cee4a74d6edcce0345dbd04a2fc0a7c3b3532988c532dd43dfeb93b0ce`.
The new measurements use 32-bit keys, correlation batches of 1,048,576, and two
concurrent coroutine batches. Both parties run on one OS thread in the local
experiments. Every reported run verified the result after measurement.

| n/list | Protocol | Online MiB | Offline MiB | Online s | Offline s | Depth bound |
|---:|---|---:|---:|---:|---:|---:|
| 131,072 | Pi | 247.030 | 508.207 | 0.287 | 42.070 | 210 |
| 131,072 | Batcher | 239.071 | 134.018 | 0.201 | 31.098 | 144 |
| 262,144 | Pi | 506.045 | 1,021.829 | 0.731 | 84.822 | 220 |
| 262,144 | Batcher | 512.267 | 286.967 | 0.484 | 65.585 | 152 |
| 524,288 | Pi | 1,038.246 | 2,056.896 | 1.525 | 172.044 | 230 |
| 524,288 | Batcher | 1,094.284 | 612.993 | 1.168 | 141.222 | 160 |
| 1,048,576 | Pi | 2,125.159 | 4,139.757 | 35.897 | 347.692 | 240 |
| 1,048,576 | Batcher | 2,331.071 | 1,305.796 | 2.606 | 299.886 | 168 |

The sampled communication crossover is between 2^17 and 2^18 keys per list.
Online communication is 5.1% lower at 2^19 and 8.8% lower at 2^20; padded GMW
ANDs are 18.0% lower at 2^20. Preprocessing and online depth remain higher.

The 2^20 Pi-logstar run experienced paging: peak RSS 14.78 GiB and observed
process swap up to 2.13 GiB. Its 35.90-second online time includes paging and is
not an unpaged CPU comparison. The Batcher process peaked at 8.00 GiB RSS.
Larger inputs have not been benchmarked with the current 15-GiB WSL allocation.

The 2^19 pilot overlapped plotting-dependency installation; the paper uses the
subsequent run in `extended-scaling-pi.jsonl` for timing. Both have the same
communication count. These large points are single executions, not medians or
confidence intervals. Smaller scaling/network points retain their three trials.

The paper appends an experimental subsection to its original `evaluation.tex`.
The original 17,789 bytes are preserved exactly, and all other pre-existing paper
source files are unchanged. The four PDF plots use a common backend, matched
parameters, and actual recorded points, with the paging point labeled.

See [raw records](benchmarks/README.md), [security scope](pi-logstar-security-status.md),
and [PowerShell push commands](push-from-powershell.md).

## Reproduce the extension and figures

Run the larger measurements serially on an idle machine with the compiled
executable. The million-key Pi-logstar case exceeds the guest's physical memory
in the recorded configuration; preserve memory observations with its timing.

```sh
python3 scripts/benchmark_logstar.py --sizes 131072,524288 --bases 16 --blocks 8 \
  --batch-sizes 1048576 --trials 1 --timeout 1800 --output out/logstar/extended-scaling-pi.jsonl
python3 scripts/benchmark_logstar.py --sizes 1048576 --bases 16 --blocks 8 \
  --batch-sizes 1048576 --trials 1 --timeout 1800 --output out/logstar/extended-million-pi.jsonl
python3 scripts/benchmark_logstar.py --sizes 131072,524288,1048576 --bases 1048576 \
  --batch-sizes 1048576 --trials 1 --timeout 1800 --output out/logstar/extended-batcher.jsonl
```

For the archived figures, install Matplotlib (`sudo apt-get install python3-matplotlib`
on Ubuntu) and run from the code repository root:

```sh
python3 scripts/plot_logstar_evaluation.py \
  docs/benchmarks/final-scaling-pi.jsonl docs/benchmarks/final-scaling-batcher.jsonl \
  docs/benchmarks/final-large-pi.jsonl docs/benchmarks/final-large-batcher.jsonl \
  docs/benchmarks/final-network-pi.jsonl docs/benchmarks/final-network-batcher.jsonl \
  docs/benchmarks/extended-scaling-pi.jsonl docs/benchmarks/extended-million-pi.jsonl \
  docs/benchmarks/extended-batcher.jsonl --paged-sizes 1048576 \
  --output ../64c0aeaf1c1f5473b45f1e06/plots/implementation
```

The plotted CSV is stored next to the four PDF/PNG figures. The script rejects
mocked/unverified results, mixed binary hashes, and incomplete TCP party pairs.
The `--paged-sizes` flag labels independently observed paging; do not infer it
solely from input size when rerunning on another machine.
