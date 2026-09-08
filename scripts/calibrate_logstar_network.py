#!/usr/bin/env python3
"""Calibrate the exact isolated veth profiles used by benchmark_logstar.

Run only when protocol benchmarks are idle. Each profile records netem settings,
offload settings, unloaded ping RTT, and forward/reverse TCP application goodput.
The TCP transfer has an unmeasured warmup and uses fresh connections per trial.
"""
import argparse
import json
import pathlib
import socket
import subprocess
import sys
import time

SCRIPT = pathlib.Path(__file__).resolve()
ROOT = SCRIPT.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from benchmark_logstar import PROFILES, link, run


def recv_exact(sock, count):
    while count:
        data = sock.recv(min(count, 1024 * 1024))
        if not data:
            raise RuntimeError("Unexpected EOF in warmup/control exchange")
        count -= len(data)


def socket_details(sock):
    result = {
        "send_buffer_bytes": sock.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF),
        "receive_buffer_bytes": sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF),
    }
    if hasattr(socket, "TCP_CONGESTION"):
        result["congestion_control"] = sock.getsockopt(
            socket.IPPROTO_TCP, socket.TCP_CONGESTION, 32).split(b"\0", 1)[0].decode()
    return result


def endpoint(args):
    if args.endpoint == "receive":
        with socket.socket() as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.settimeout(args.timeout)
            listener.bind((args.address, args.port))
            listener.listen(1)
            sock, _ = listener.accept()
        with sock:
            sock.settimeout(args.timeout)
            recv_exact(sock, args.warmup_bytes)
            sock.sendall(b"W")
            start = None
            count = 0
            while True:
                data = sock.recv(1024 * 1024)
                now = time.perf_counter()
                if not data:
                    finish = now
                    break
                if start is None:
                    start = now
                count += len(data)
            if start is None or count == 0:
                raise RuntimeError("No measured TCP data received")
            elapsed = finish - start
            print(json.dumps({"endpoint": "receive", "bytes": count,
                              "seconds": elapsed, "goodput_mbit_s": count * 8 / elapsed / 1e6,
                              **socket_details(sock)}), flush=True)
    else:
        deadline = time.monotonic() + min(args.timeout, 10)
        while True:
            try:
                sock = socket.create_connection((args.address, args.port), timeout=1)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
        with sock:
            sock.settimeout(args.timeout)
            payload = bytes(1024 * 1024)
            remaining = args.warmup_bytes
            while remaining:
                chunk = min(remaining, len(payload))
                sock.sendall(memoryview(payload)[:chunk])
                remaining -= chunk
            recv_exact(sock, 1)
            start = time.perf_counter()
            count = 0
            while time.perf_counter() - start < args.seconds:
                sock.sendall(payload)
                count += len(payload)
            sock.shutdown(socket.SHUT_WR)
            # Wait for the receiver to close, so sender elapsed includes draining
            # bytes already queued in its TCP buffer and emulated link.
            while sock.recv(1024):
                pass
            elapsed = time.perf_counter() - start
            print(json.dumps({"endpoint": "send", "bytes": count,
                              "seconds": elapsed, "goodput_mbit_s": count * 8 / elapsed / 1e6,
                              **socket_details(sock)}), flush=True)


def inspect(name):
    interfaces = json.loads(run("ip", "-n", name, "-j", "link", "show").stdout)
    device = next(item["ifname"] for item in interfaces if item["ifname"] != "lo")
    return {
        "namespace": name,
        "interface": device,
        "links": interfaces,
        "qdisc": json.loads(run("ip", "netns", "exec", name,
                                "tc", "-s", "-j", "qdisc", "show", "dev", device).stdout),
        "offload_features": run("ip", "netns", "exec", name,
                                "ethtool", "-k", device).stdout,
    }


def transfer(names, sender, args):
    receiver = 1 - sender
    address = f"10.203.0.{receiver + 1}"
    common = [sys.executable, str(SCRIPT), "--address", address,
              "--port", str(args.port), "--seconds", str(args.seconds),
              "--warmup-bytes", str(args.warmup_bytes), "--timeout", str(args.timeout)]
    processes = []
    try:
        for role, mode in ((receiver, "receive"), (sender, "send")):
            processes.append(subprocess.Popen(
                ["ip", "netns", "exec", names[role], *common, "--endpoint", mode],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
        result = []
        deadline = time.monotonic() + args.timeout
        for proc in processes:
            stdout, stderr = proc.communicate(timeout=max(1, deadline - time.monotonic()))
            if proc.returncode:
                raise RuntimeError(stderr + stdout)
            result.append(json.loads(stdout))
        if result[0]["bytes"] != result[1]["bytes"]:
            raise RuntimeError("TCP calibration byte counts differ")
        return {"sender_party": sender, "receiver": result[0], "sender": result[1]}
    finally:
        for proc in processes:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.communicate(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.communicate()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profiles", default="lan,wan,slow")
    parser.add_argument("--trials", type=int, default=1)
    parser.add_argument("--seconds", type=float, default=2)
    parser.add_argument("--warmup-bytes", type=int, default=1024 * 1024)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--port", type=int, default=12124)
    parser.add_argument("--address", default="10.203.0.1")
    parser.add_argument("--endpoint", choices=["send", "receive"])
    parser.add_argument("--output", type=pathlib.Path,
                        default=ROOT / "out/logstar/network-calibration.jsonl")
    args = parser.parse_args()
    if args.seconds <= 0 or args.trials < 1 or args.warmup_bytes < 0:
        parser.error("seconds/trials must be positive and warmup-bytes nonnegative")
    if args.endpoint:
        endpoint(args)
        return
    profiles = args.profiles.split(",")
    if any(profile not in PROFILES for profile in profiles):
        parser.error("unknown profile")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("a", encoding="utf-8") as out:
        def emit(record):
            out.write(json.dumps(record) + "\n")
            out.flush()
            print(json.dumps(record), flush=True)
        emit({"type": "calibration_environment", "seconds": args.seconds,
              "warmup_bytes": args.warmup_bytes, "trials": args.trials,
              "profiles": profiles, "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
        for profile in profiles:
            with link(profile) as names:
                emit({"type": "link_configuration", "profile": profile,
                      "nominal_mbit_s_each_direction": PROFILES[profile][0],
                      "added_rtt_ms": PROFILES[profile][1],
                      "endpoints": [inspect(name) for name in names]})
                # Populate ARP/neighbor entries before measuring unloaded RTT.
                run("ip", "netns", "exec", names[0], "ping", "-c", "1", "-W", "5", "10.203.0.2")
                ping = run("ip", "netns", "exec", names[0], "ping",
                           "-c", "5", "-i", "0.1", "-W", "5", "10.203.0.2").stdout
                emit({"type": "unloaded_rtt", "profile": profile, "ping_stdout": ping})
                for trial in range(args.trials):
                    for sender in (0, 1):
                        emit({"type": "tcp_goodput", "profile": profile, "trial": trial,
                              **transfer(names, sender, args)})
                emit({"type": "link_statistics_after", "profile": profile,
                      "endpoints": [inspect(name) for name in names]})


if __name__ == "__main__":
    main()
