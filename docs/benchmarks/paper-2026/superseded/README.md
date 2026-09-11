# Superseded shared-stream pilot

These measurements are retained for traceability and are excluded from the
published study. They came from the earlier executable with SHA-256
`689e3505afc17c56cff0df96f65e56c34873a9b9c333d4b619fac7ed98b9b6bd`.

The pilot used one TCP stream for multiple logical channels and one correlation
batch in flight. The LAN Median execution at `n=2^18` stalled: one party was
waiting for socket activity and the other had returned from its I/O event loop
while the protocol task remained incomplete. The socket scheduler could wait
for a protocol receive that depended on correlation messages queued behind it
on the same stream.

The final harness gives correlation generation and protocol execution separate
data streams, both subject to the same per-party link rate. It uses two
correlation batches in flight uniformly across methods and fails immediately
if the I/O loop returns with an unfinished protocol task. Small correctness
probes and the three largest LAN Median cases passed with that harness. All
published timings are collected again with the corrected executable; none of
the pilot timings are reused.
