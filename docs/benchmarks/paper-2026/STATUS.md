# Study status: complete

Completed on September 11, 2026: all 390 configurations, 930 timed
executions, and 130 separate fresh-cryptography round audits passed the
strict summarizer. Every power of two from `2^8` through `2^20` is included
for local, LAN, and WAN transport and all matched input shapes.

- All output checks passed; accepted executions have no observed Linux process
  swapping and no quicksort correlation-reserve refills.
- Three trials are reported through `2^16`; larger sizes have one trial each.
- One frozen executable was used, with SHA-256:
  `d1dbb7f48e5e1f989814742eb666b75c6f1d8f50fce3c952e8248f4e6dc7c79e`.
- The measured evaluation is installed in the paper. The previous evaluation,
  abstract, introduction, related work, and overview are preserved here before
  updating their old benchmark claims and references.
- The section PDF (7 pages) and complete-results PDF (13 pages) are saved in
  workspace `output/pdf/`. Both were compiled, rendered, and visually checked;
  all fonts are embedded and neither has overflow or unresolved references.
- The full manuscript compiles successfully. Existing theory layout/font
  warnings remain outside the new evaluation.
- Complete CSV, aggregate JSON, all-size tables, and vector/PNG figures are in
  the paper's `plots/benchmark/` directory. Exact public schedules and raw
  measurements are retained here, along with reproduction scripts in `scripts/`.
- Earlier shared-stream pilot results are preserved under `superseded/` and
  excluded from the final study.

## Machine restoration

The original WSL configuration was absent. After all experiments and PDF
checks finished, the temporary configuration was verified against
`out/paper-wslconfig-26gb`, removed, and WSL was shut down. The original
memory defaults and stopped state are restored. No benchmark processes,
running containers, or benchmark network namespaces remained. Host network
interfaces and qdiscs were not modified.

See `provenance.json` for experiment identification and
`artifact-verification.json` for final PDF hashes and verification details.
