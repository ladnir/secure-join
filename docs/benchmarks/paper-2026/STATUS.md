# Study status: complete, with Median revision

The paper uses a fully validated 390-configuration study: 930 timed executions and 130 fresh-cryptography round audits, covering every n = 2^8 through 2^20 on local, LAN, and WAN and all matched input shapes. The Median-only replacement contains 93 timed executions and 13 audits. All output checks passed; all accepted timed records have zero sampled Linux process swap. The largest untimed Median audit used explicitly recorded paging; none of its elapsed time is published.

The timed benchmark executable and cryptographic settings are unchanged. All non-Median observations and root artifacts match their earlier versions. A later update to Section 8.3 retains BBDLO analytical estimates and uses exact C++ comparison counts for our implemented benchmark schedules. The paper reports 121 online dependency rounds for Median at n = 2^20, versus Batcher's 147, with 2.03 times its online traffic. At n = 2^11 Median has 47 versus 84 rounds and a 1.45 times WAN online speedup. Large-input Median remains slower; the paper states this tradeoff.

The actual paper (51 pages), section preview (7 pages), and complete tables/runtime curves (13 pages) compile and were visually checked. Final PDFs are saved under workspace `output/pdf/` and the paper's conventional `output/pdf/`. No new evaluation overflow or unresolved references were found. Existing theory font/layout warnings are unchanged.

See `active-median-revision.json`, `median-retune-20260911/validation.json`, and `artifact-verification.json` for exact provenance and checks. Original raw study records remain immutable. Final restoration of the temporary 26 GiB WSL configuration is recorded in the verification file and rerun checkpoint.

Machine restoration is complete: the task-only `.wslconfig` was verified and removed, WSL was shut down, and no benchmark processes, observers, containers, or benchmark namespaces remained. The original memory defaults and stopped state are restored.

Exact comparison accounting is complete for all 26 selected Logstar/Median schedules. At n = 2^20, the counts are 11,010,049 and 56,655,872, respectively. Section 8.3 and both paper PDFs use these counts; BBDLO remains analytically estimated. All 32 pre-existing plot/data/table artifacts are byte-for-byte unchanged.
