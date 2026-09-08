# Raw Pi-logstar measurements

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
[the results report](../logstar-results.md).
