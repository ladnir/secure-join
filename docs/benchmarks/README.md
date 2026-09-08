# Raw Pi-logstar measurements

The optimized `optimize-pi-logstar` evaluation uses:

- `packed-scaling.jsonl`: every power of two from 2^10 through 2^20, three paired
  serial executions per protocol and size, with matched 2^20 correlation batches.
- `packed-schedule.json`: public cost counts for blocks 2, 4, 8, and 16 at each
  size. These are circuit schedules, not executed benchmark measurements.
- `packed-network-{12,16}.jsonl`: three paired TCP trials per protocol on LAN,
  WAN, and slow WAN at 2^12 and 2^16 keys/list.
- `packed-network-20.jsonl`: one paired TCP WAN trial at 2^20 keys/list.
- `packed-batch-sweep.jsonl`, `packed-batcher-batch-sweep.jsonl`: matched 2^20
  and 2^22 correlation batches at 2^14 and 2^16, one pilot per configuration.
- `packed-selftest.jsonl`: real-crypto validation from the measured executable.
- `packed-network-calibration.jsonl`: unloaded RTT, bidirectional TCP goodput,
  offload settings, and qdisc statistics for the isolated link profiles.

Optimized measurements use executable SHA-256
`769ba0491bdd740616f500dcd0f1acab78dfdb944ed777cc3bbcf9953287c0b5`.
Local trials record peak process RSS and maxima from 0.5-second RSS/swap samples.
They execute both parties on one OS thread. Network trials use separate processes,
with two Boost.Asio I/O workers each, and record memory maxima for each party.
The files below preserve the earlier implementation's results; do not pool
the two executable builds in a timing table or plot.

The earlier builds predate the correction of modulo bias in `Perm::randomize`.
Their measurements are retained as development diagnostics; they do not support
the corrected implementation's uniform private-permutation security premise.

The `final-*.jsonl` files are raw measurements from the final executable identified
by SHA-256 in each record. Each run begins with its command line, environment,
profile definitions, and executable hash. All reported protocol trials use real
cryptography and verify the opened synthetic output after the measured phases.

- `final-scaling-{pi,batcher}.jsonl`: 32-bit keys, 1,024–65,536 keys per list,
  three trials per configuration, in-process transport.
- `final-large-{pi,batcher}.jsonl`: 262,144 keys per list, one trial each,
  correlation batches of 1,048,576.
- `final-parameters.jsonl`: 32/64-bit keys and three correlation batch sizes,
  two trials per configuration.
- `final-network-{pi,batcher}.jsonl`: independent TCP processes over isolated
  Linux `tc` profiles, three trials per configuration. Pair records by
  `trial_id`, then sum the parties' **sent** bytes once; do not add received
  counters again.
- `network-calibration.jsonl`: unloaded ping RTT, forward/reverse bulk TCP
  goodput, qdisc statistics, and offload settings for the same link profiles.
  Reproduce with `sudo python3 scripts/calibrate_logstar_network.py`; it also
  requires `ethtool` and `ping`.
- `extended-scaling-pi.jsonl`, `extended-million-pi.jsonl`, and
  `extended-batcher.jsonl`: additional matched sizes through 1,048,576 keys per
  list, one execution per configuration. The million-key Pi-logstar execution
  experienced paging; its elapsed time is labeled accordingly in the paper.
- `million-memory.jsonl`: two-second `/proc` samples attached during that
  million-key run. The observed maximum process swap was 2,233,600 KiB.
- `extended-large-pi.jsonl`: the initial 524,288-key pilot, retained as a
  diagnostic record. Plotting-dependency installation overlapped part of this
  pilot; the paper uses the subsequent `extended-scaling-pi.jsonl` run for its
  timing. Its online communication count is unchanged.

`initial-sweep.jsonl` and `block-sweep-optimized.jsonl` are intermediate
development builds, retained only as evidence for optimization and parameter
comparisons. They are not part of the final timing tables. The initial sweep
predates automatic binary-hash recording; the block sweep predates the final
byte-packing optimization.

The summarizer rejects unverified results, different binary hashes, and incomplete
TCP party pairs. It does not distinguish machines or changed profile definitions;
do not pool measurements from different environments. For example, from the repository root:

```sh
python3 scripts/summarize_logstar.py docs/benchmarks/final-scaling-pi.jsonl \
  docs/benchmarks/final-scaling-batcher.jsonl
```

Interpretation, measurement limits, and commands to rerun the experiments are in
[the optimized results report](../packed-results.md) and
[the archived results report](../logstar-results.md).
