#!/usr/bin/env python3
"""Independent plaintext specification of the paper's duplicated-median recursion.

No cryptography or performance measurements. Checks alignment, exact scatter
rank ranges, surviving original indices, arbitrary tie patterns and padding.
"""
import bisect
import itertools
import random


def align(x, y, child, source_only=False, original_n=0):
    n = len(x)
    medians = x[child - 1::child]
    key = lambda row: (row[0], int(row[1] >= original_n)) if source_only else row[:2]
    keys = lambda run: [key(row) for row in run]
    xs, ys = keys(medians), keys(y)
    out = [None] * (2 * n)
    ranks = []
    for i, median in enumerate(medians):
        offset = bisect.bisect_left(ys, key(median))
        for t in range(child):
            rank = i * child + t + offset
            assert out[rank] is None
            out[rank] = (*median[:2], False)
            ranks.append(rank)
    for j, row in enumerate(y):
        rank = j + child * bisect.bisect_right(xs, key(row))
        assert out[rank] is None
        out[rank] = row
        ranks.append(rank)
    assert sorted(ranks) == list(range(2 * n))
    assert keys(out) == sorted(keys(out))
    return out


def protocol(x, y, base=4, children=(), source_only=False):
    n = len(x)
    assert n == len(y) and n > 0
    padded = 1 << (n - 1).bit_length()
    infinity = max(x + y) + 1
    a = [(v, i, True) for i, v in enumerate(x)]
    b = [(v, n + i, True) for i, v in enumerate(y)]
    a += [(infinity, i, False) for i in range(n, padded)]
    b += [(infinity, padded + i, False) for i in range(n, padded)]

    def recurse(a, b, depth):
        key = lambda row: (row[0], int(row[1] >= n)) if source_only else row[:2]
        size = len(a)
        if size <= base:
            return sorted(a + b, key=key)
        child = children[depth] if depth < len(children) else 0
        child = child or 1 << (2 * (size.bit_length() - 1) // 3)
        z, zz = align(a, b, child, source_only, n), align(b, a, child, source_only, n)
        assert list(map(key, z[child-1::child])) == list(map(key, zz[child-1::child]))
        out = []
        for i in range(0, 2 * size, child):
            out.extend(recurse(z[i:i+child], zz[i:i+child], depth + 1))
        assert list(map(key, out)) == sorted(map(key, out))
        return out

    result = [index for _, index, real in recurse(a, b, 0) if real]
    expected = sorted(range(2 * n), key=lambda i: ((x+y)[i], i))
    assert result == expected, (x, y, base, children, result, expected)
    return result


def check_leaf_networks():
    count = 0
    for n in (1, 2, 4, 8):
        runs = list(itertools.combinations_with_replacement(range(3), n))
        for x in runs:
            for y in runs:
                original = list(zip(x+y, range(2*n)))
                for odd_even in (False, True):
                    work = original[:n] + (original[n:] if odd_even else original[n:][::-1])
                    distance, comparisons = n, 0
                    while distance:
                        offset = distance if odd_even and distance < n else 0
                        touched = set()
                        for base in range(offset, 2*n-offset, 2*distance):
                            for i in range(distance):
                                l, r = base+i, base+i+distance
                                assert l not in touched and r not in touched
                                touched.update((l, r))
                                if distance == n:
                                    # Initial local positions are public: ties
                                    # stay left, and their XOR is a constant.
                                    assert work[l][1] < work[r][1]
                                    assert work[l][1] ^ work[r][1] == (n if odd_even else 2*n-1)
                                    swap = work[r][0] < work[l][0]
                                else:
                                    swap = work[r] < work[l]
                                if swap:
                                    work[l], work[r] = work[r], work[l]
                                comparisons += 1
                        distance //= 2
                    assert work == sorted(original)
                    assert comparisons == (n*(n.bit_length()-1)+1 if odd_even else n*n.bit_length())
                    count += 1
    return count


def main():
    count = 0
    for n in range(1, 10):
        runs = list(itertools.combinations_with_replacement(range(3), n))
        for x in runs:
            for y in runs:
                for base in (1, 2, 4, 16):
                    for source_only in (False, True):
                        protocol(list(x), list(y), base, source_only=source_only)
                        count += 1
    rng = random.Random(54161)
    for n in (16, 17, 31, 32, 63, 64, 65, 128, 257, 1024):
        for _ in range(30):
            x = sorted(rng.randrange(16) for _ in range(n))
            y = sorted(rng.randrange(16) for _ in range(n))
            protocol(x, y, rng.choice((1, 2, 4, 8, 16, 32)), source_only=bool(count%2))
            count += 1
    for children in ((32, 8, 2), (16, 4, 1), (2,), (1,), (8, 2), (4,)):
        for _ in range(30):
            x = sorted(rng.randrange(4) for _ in range(64))
            y = sorted(rng.randrange(4) for _ in range(64))
            protocol(x, y, 1, children, source_only=bool(count%2))
            count += 1
    network_count = check_leaf_networks()
    print(f"Pi-median reference: {count} protocol cases and {network_count} stable leaf-network cases passed")


if __name__ == '__main__':
    main()
