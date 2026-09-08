#!/usr/bin/env python3
"""Independent checks of packed block invariants and extraction openings."""
from collections import Counter
from itertools import combinations, combinations_with_replacement, permutations, product
import random


def merge(x, y, m):
    n = len(x)
    data = [(v, source, j, source * n + j)
            for source, values in enumerate((x, y)) for j, v in enumerate(values)]
    blocks = [data[i:i + m] for i in range(0, 2 * n, m)]
    blocks.sort(key=lambda b: b[0])
    previous_opposite = None
    result = []
    for i, b in enumerate(blocks):
        if i and blocks[i - 1][0][1] != b[0][1]:
            previous_opposite = blocks[i - 1]
        s = previous_opposite or blocks[0]
        hi = blocks[i + 1][0][:2] if i + 1 < len(blocks) else (float('inf'), 1)
        lo = b[0][:2]
        records = []
        for origin, values in enumerate((b, s)):
            for offset, row in enumerate(values):
                active = row[:2] <= hi and (not origin or (previous_opposite is not None and row[:2] >= lo))
                # Local comparison carries only (key, original source, offset).
                records.append((row[0], row[1], offset, origin, active))
        records.sort(key=(lambda r: r[:3]) if previous_opposite is not None else (lambda r: (r[3], r[2])))
        for local_rank, (_, _, offset, origin, active) in enumerate(records):
            if active:
                source_block = (b, s)[origin]
                reconstructed = (source_block[0][3] // m) * m + offset
                global_rank = (b[0][3] % n) + (s[0][3] % n) + local_rank
                assert global_rank == len(result), (x, y, m, i, global_rank, len(result))
                result.append(reconstructed)
    expected = [r[3] for r in sorted(data)]
    assert result == expected, (x, y, m, result, expected)


def main():
    cases = 0
    for n, alphabet in ((4, 4), (8, 3), (16, 2)):
        values = list(combinations_with_replacement(range(alphabet), n))
        for x, y in product(values, repeat=2):
            for m in (2, 4, 8):
                if m < n:
                    merge(x, y, m)
                    cases += 1
    rng = random.Random(984617)
    for _ in range(2500):
        n = 1 << rng.randrange(2, 11)
        m = 1 << rng.randrange(1, min(n.bit_length() - 1, 7))
        alphabet = rng.choice((2, 7, 2**32))
        x = sorted(rng.randrange(alphabet) for _ in range(n))
        y = sorted(rng.randrange(alphabet) for _ in range(n))
        merge(x, y, m)
        cases += 1
    # The opened shuffled flag/rank transcript has exactly the same law for
    # every placement of two active rows among four. Inactive ranks are omitted.
    laws = []
    for positions in combinations(range(4), 2):
        ranks = {p: r for r, p in enumerate(positions)}
        laws.append(Counter(tuple(ranks.get(p, -1) for p in pi) for pi in permutations(range(4))))
    assert all(law == laws[0] for law in laws)
    print(f"packed reference: {cases} stable merges passed; 6 exhaustive extraction transcript laws agree")


if __name__ == '__main__':
    main()
