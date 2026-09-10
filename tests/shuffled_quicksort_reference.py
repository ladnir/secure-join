#!/usr/bin/env python3
"""Independent plaintext oracle; no cryptography or benchmark execution."""
from collections import Counter
from itertools import permutations
import random


def sort_and_trace(keys, shuffle, terminal, pivots=1):
    records = [(keys[i], i) for i in shuffle]
    work = [(0, len(records))] if len(records) > 1 else []
    transcript = []
    while work:
        following, wave = [], []
        for begin, end in work:
            size = end - begin
            if size <= terminal:
                ranks = [0] * size
                for i in range(size):
                    for j in range(i + 1, size):
                        less = records[begin + i] < records[begin + j]
                        wave.append(int(less))
                        ranks[i] += not less
                        ranks[j] += less
                assert sorted(ranks) == list(range(size))
                ordered = [None] * size
                for i, rank in enumerate(ranks):
                    ordered[rank] = records[begin + i]
                records[begin:end] = ordered
            else:
                count = min(pivots, size - 1)
                sample = records[end - count:end]
                for i in range(count):
                    for j in range(i + 1, count):
                        wave.append(int(sample[i] < sample[j]))
                buckets = [[] for _ in range(count + 1)]
                for row in records[begin:end - count]:
                    outcomes = [row < pivot for pivot in sample]
                    wave.extend(map(int, outcomes))
                    buckets[count - sum(outcomes)].append(row)
                ordered, offset = [], begin
                for j, bucket in enumerate(buckets):
                    if len(bucket) > 1:
                        following.append((offset, offset + len(bucket)))
                    ordered.extend(bucket)
                    if j < count:
                        ordered.append(sorted(sample)[j])
                    offset += len(bucket) + 1
                records[begin:end] = ordered
        transcript.append(tuple(wave))
        work = following
    result = [i for _, i in records]
    assert result == sorted(range(len(keys)), key=lambda i: (keys[i], i))
    return tuple(transcript)


def main():
    cases = 0
    # Every hidden permutation: equality patterns, reversed keys and distinct
    # keys must induce EXACTLY the same distribution of opened wave transcripts.
    for n in range(8):
        for terminal, pivots in ((t, p) for t in (2, 4, 8) for p in (1, 3)):
            reference = None
            for keys in ([0] * n, list(range(n)), list(reversed(range(n))),
                         [i % 2 for i in range(n)]):
                traces = Counter()
                for shuffle in permutations(range(n)):
                    traces[sort_and_trace(keys, shuffle, terminal, pivots)] += 1
                    cases += 1
                if reference is None:
                    reference = traces
                else:
                    assert traces == reference, (n, terminal, keys)
    rng = random.Random(711)
    for n in (9, 17, 32, 65, 129, 257):
        for terminal in (2, 3, 8, 16, 32):
            for _ in range(20):
                keys = [rng.randrange(16) for _ in range(n)]
                shuffle = list(range(n))
                rng.shuffle(shuffle)
                sort_and_trace(keys, shuffle, terminal, rng.choice((1, 3)))
                cases += 1
    print(f"PASS: {cases} plaintext cases; exhaustive transcript distributions through n=7")


if __name__ == "__main__":
    main()
