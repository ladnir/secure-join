#!/usr/bin/env python3
"""Bounded development checks. No paper-sized crypto runs or benchmark sweep."""
import argparse
import json
from pathlib import Path
import socket
import subprocess


def run(exe, *args):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True,
                            text=True, timeout=120, check=True)
    return json.loads(result.stdout)


def tcp(exe, args, mismatch=False):
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        address = f"127.0.0.1:{probe.getsockname()[1]}"
    common = [str(exe), *map(str, args), "--address", address]
    # asioConnect's client retries while the server finishes starting.
    p0 = subprocess.Popen(common + ["--party", "0"], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
    p1 = subprocess.Popen(common + (["--terminal", "3"] if mismatch else [])
                          + ["--party", "1"], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
    try:
        a, ae = p0.communicate(timeout=120)
        b, be = p1.communicate(timeout=120)
        if mismatch:
            assert p0.returncode and p1.returncode
            assert "Public configurations differ" in ae and "Public configurations differ" in be
            return
        assert p0.returncode == p1.returncode == 0, (ae, be)
        a, b = json.loads(a), json.loads(b)
        assert a["verified"] and b["verified"]
        for field in ("comparisons", "comparison_batches", "padded_ands", "refills",
                      "online_rounds_without_refills", "online_payload_bytes_without_refills"):
            assert a[field] == b[field], field
        measured = sum(x["online_sent_bytes_including_refills"] for x in (a, b))
        predicted = a["online_payload_bytes_without_refills"] + sum(
            x["refill_sent_bytes_this_party"] for x in (a, b))
        assert measured >= predicted
        if "--reserve" in args:
            assert a["refills"] > 0
    finally:
        for process in (p0, p1):
            if process.poll() is None:
                process.kill()
                process.communicate()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path("out/build/linux/frontend/shuffledquicksort"))
    opts = parser.parse_args()
    exe = opts.exe.resolve()
    # LARGE plans are arithmetic only and do not allocate correlation batches.
    for bits in (32, 128):
        for n in (1024, 1048576, 2097152):
            plan = run(exe, "--n", n, "--bits", bits, "--plan")
            assert plan["type"] == "public_plan"
            width = bits + (n - 1).bit_length()
            assert plan["comparison_bits"] == width
            assert plan["rounds_per_comparison"] == (width - 1).bit_length()
            assert plan["ands_per_comparison"] < 2 * width
            assert plan["shuffle_payload_bytes"] == 2 * n * ((width + 7) // 8)
    for args in (["--n", 33, "--bits", 128, "--pattern", "equal"],
                 ["--n", 257, "--bits", 32, "--left", 65, "--pattern", "duplicates"],
                 ["--n", 129, "--bits", 7, "--reserve", 1]):
        out = run(exe, *args)
        assert out["verified"] and out["real_crypto"]
        assert out["online_sent_bytes_including_refills"] >= out["online_payload_bytes_without_refills"]
    tcp(exe, ["--n", 33, "--bits", 128, "--pattern", "duplicates"])
    tcp(exe, ["--n", 129, "--bits", 7, "--reserve", 1])
    tcp(exe, ["--n", 129, "--bits", 128, "--pivots", 3, "--left", 17])
    tcp(exe, ["--n", 33, "--bits", 32], mismatch=True)
    print("PASS: 6 public plans, 3 small local runs, 3 TCP runs, configuration mismatch rejection")


if __name__ == "__main__":
    main()
