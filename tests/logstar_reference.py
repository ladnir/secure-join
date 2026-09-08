#!/usr/bin/env python3
"""Plaintext correctness oracle for the two-list Pi-Logstar construction.

This is a test model, not a secure backend.  It deliberately preserves every
physical key and only changes ``real`` flags.  Consequently every recursive
input stays sorted and its first physical row is a valid block minimum even
when all rows in that block are dummy.  The production implementation can then
select minima locally instead of using the paper's interactive first-real Med.

The implementation follows constructionone.tex's block merge, transition-block
copy, half-open range masking, and parallel recursion.  Sorting here models the
secure median/base merge; linear prefix copying models the aggregation tree.
"""

from dataclasses import dataclass, replace
from itertools import combinations_with_replacement, product
import argparse
import random


@dataclass(frozen=True)
class Row:
    value: int
    index: int
    real: bool = True
    infinity: int = 0

    @property
    def key(self):
        # The explicit sentinel field permits both zero and maximal keys.
        return (self.infinity, self.value, self.index)


def _power_of_two(n):
    return 1 << max(0, (n - 1).bit_length())


def _recursive(x, y, cutoff, block_override, check):
    n = len(x)
    assert n == len(y) and n > 0 and n & (n - 1) == 0
    if check:
        assert all(a.key <= b.key for a, b in zip(x, x[1:]))
        assert all(a.key <= b.key for a, b in zip(y, y[1:]))
        before = sorted(r.index for r in x + y if r.real)
        assert len(before) == len(set(before))

    m = min(n, block_override or _power_of_two((n - 1).bit_length()))
    if n <= cutoff or m >= n:
        result = sorted(x + y, key=lambda r: r.key)
    else:
        assert n % m == 0
        # The source bit labels the current recursive inputs, not ownership.
        blocks = [(rows[i:i + m], source)
                  for source, rows in enumerate((x, y))
                  for i in range(0, n, m)]
        # Reverse equal minima's source order to exercise an unstable secure
        # merge network (a dummy carry can share its keys with real rows).
        blocks.sort(key=lambda b: (b[0][0].key, -b[1]))

        # Prefix scan: a transition starts a streak; otherwise repeat its carry.
        empty = [replace(r, real=False) for r in blocks[0][0]]
        carries = []
        carry = empty
        for i, (block, source) in enumerate(blocks):
            if i and source != blocks[i - 1][1]:
                carry = blocks[i - 1][0]
            carries.append(carry)

        result = []
        for i, ((block, source), carry) in enumerate(zip(blocks, carries)):
            del source
            lower = block[0].key
            upper = blocks[i + 1][0][0].key if i + 1 < len(blocks) else None
            in_upper = lambda r: upper is None or r.key < upper
            own = [replace(r, real=r.real and in_upper(r)) for r in block]
            stray = [replace(r, real=r.real and lower <= r.key and in_upper(r))
                     for r in carry]
            result.extend(_recursive(own, stray, cutoff, None, check))

    if check:
        after = [r for r in result if r.real]
        assert sorted(r.index for r in after) == before
        assert all(a.key <= b.key for a, b in zip(after, after[1:]))
    return result


def merge_indices(x, y, cutoff=10, block_size=None, check=True):
    """Return stable gather indices for two equal-length sorted public lists."""
    if len(x) != len(y):
        raise ValueError("Pi-Logstar expects equal-length input lists")
    if cutoff < 1:
        raise ValueError("cutoff must be positive")
    if block_size is not None and (block_size < 2 or block_size & (block_size - 1)):
        raise ValueError("block_size must be a power of two of at least 2")
    if x != sorted(x) or y != sorted(y):
        raise ValueError("input lists must be sorted")
    if not x:
        return []
    original_n = len(x)
    n = _power_of_two(original_n)
    xr = [Row(value, i) for i, value in enumerate(x)]
    yr = [Row(value, original_n + i) for i, value in enumerate(y)]
    padding = [Row(0, 0, False, 1)] * (n - original_n)
    return [r.index for r in _recursive(xr + padding, yr + padding, cutoff,
                                       block_size, check) if r.real]


def selftest(seed=20260907, trials=100):
    rng = random.Random(seed)
    cases = 0

    def verify(x, y, cutoff=2, block_size=None):
        nonlocal cases
        values = x + y
        expected = sorted(range(len(values)), key=lambda i: (values[i], i))
        actual = merge_indices(x, y, cutoff=cutoff, block_size=block_size)
        assert actual == expected, (x, y, cutoff, block_size, actual, expected)
        cases += 1

    # Exhaust duplicates and transition patterns at short sizes.
    for n in range(9):
        alphabet = range(3) if n <= 5 else range(2)
        lists = list(combinations_with_replacement(alphabet, n))
        for x, y in product(lists, repeat=2):
            verify(list(x), list(y))

    maximum = (1 << 128) - 1
    for n in [1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127,
              128, 129, 255, 256, 257]:
        patterns = [([0] * n, [0] * n), ([maximum] * n, [maximum] * n),
                    ([0] * n, [maximum] * n), ([maximum] * n, [0] * n),
                    (list(range(n)), list(range(n))),
                    (list(range(0, 2 * n, 2)), list(range(1, 2 * n, 2))),
                    (list(range(n)), list(range(n, 2 * n))),
                    (list(range(n, 2 * n)), list(range(n)))]
        for _ in range(trials):
            modulus = rng.choice([2, 3, 10, maximum])
            patterns.append((sorted(rng.randrange(modulus) for _ in range(n)),
                             sorted(rng.randrange(modulus) for _ in range(n))))
        for x, y in patterns:
            cutoff = rng.choice([1, 2, 4, 10, 16])
            verify(x, y, cutoff)
        # Stress different public recursion parameters independently of data.
        for block_size in [2, 4, 8, 16]:
            verify(*patterns[rng.randrange(len(patterns))], cutoff=1,
                   block_size=block_size)
    return cases


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=20260907)
    parser.add_argument("--trials", type=int, default=100)
    options = parser.parse_args()
    count = selftest(options.seed, options.trials)
    print(f"Pi-Logstar plaintext reference: {count} cases passed (seed={options.seed})")
