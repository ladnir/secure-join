#!/usr/bin/env python3
"""Plaintext oracles for the two asymmetric protocols and unequal Batcher."""
import bisect
import itertools
import random


def odd_even(a, b):
    if not a: return b[:]
    if not b: return a[:]
    if len(a) == len(b) == 1: return sorted(a + b)
    even, odd = odd_even(a[::2], b[::2]), odd_even(a[1::2], b[1::2])
    out = []
    for i in range(len(even)):
        out.append(even[i])
        if i < len(odd): out.append(odd[i])
    for i in range(1, len(out)-1, 2):
        if out[i] > out[i+1]: out[i], out[i+1] = out[i+1], out[i]
    return out


def merge_ranks(x, y, block, cube):
    m, n = len(x), len(y)
    blocks = [y[i:i+block] for i in range(0, n, block)]
    # The last block also receives every X greater than all of Y.
    target = [bisect.bisect_left([v[-1] for v in blocks[:-1]], value) for value in x]
    first = [i == 0 or target[i] != target[i-1] for i in range(m)]
    tags = [0] * (len(blocks) + m)
    for i in range(m):
        tags[target[i] if first[i] else len(blocks)+i] = i+1
    assert sorted(tags) == [0]*len(blocks) + list(range(1,m+1))
    selected = [blocks[target[i]] if first[i] else [] for i in range(m)]
    coarse = [bisect.bisect_right(x, v[-1]) for v in blocks]
    xr = [0]*m
    delta = [[0]*len(v) for v in blocks]
    if cube:
        extracted = [value for v in selected for value in v]
        groups = 0
        for i in range(m):
            groups += first[i]
            r = sum(value < x[i] for value in extracted)
            xr[i] = i + target[i]*block + r - (groups-1)*block
        for i in range(m):
            if first[i]:
                delta[target[i]] = [bisect.bisect_right(x, value)-coarse[target[i]] for value in selected[i]]
    else:
        copied = []
        for i in range(m):
            copied.append(selected[i] if first[i] else copied[-1])
            xr[i] = i + target[i]*block + bisect.bisect_left(copied[i], x[i])
        suffix = [[int(x[i] <= value) for value in copied[i]] for i in range(m)]
        for i in range(m-2, -1, -1):
            if not first[i+1]: suffix[i] = [a+b for a,b in zip(suffix[i],suffix[i+1])]
        for i in range(m):
            if first[i]: delta[target[i]] = [v+i-coarse[target[i]] for v in suffix[i]]
    yr = [j*block+t+coarse[j]+delta[j][t] for j in range(len(blocks)) for t in range(len(blocks[j]))]
    ranks = xr+yr
    out = [None]*(m+n)
    for i, rank in enumerate(ranks):
        assert 0 <= rank < m+n and out[rank] is None
        out[rank] = i
    return out


def main():
    count = 0
    for m in range(1, 7):
        for n in range(1, 9):
            # Zero-one principle covers the public comparator network; tags
            # impose stable total order even for all-equal original keys.
            for a in range(m+1):
                for b in range(n+1):
                    x, y = [0]*a+[1]*(m-a), [0]*b+[1]*(n-b)
                    expected = sorted(range(m+n), key=lambda i: ((x+y)[i], i))
                    pairs = [(v,i) for i,v in enumerate(x+y)]
                    assert [i for _,i in odd_even(pairs[:m], pairs[m:])] == expected
                    for block in (1,2,4,8):
                        for cube in (False,True):
                            assert merge_ranks(x,y,block,cube) == expected
                            count += 1
    rng = random.Random(23917)
    for _ in range(2000):
        m,n = rng.randrange(1,65),rng.randrange(1,257)
        x,y = sorted(rng.randrange(32) for _ in range(m)), sorted(rng.randrange(32) for _ in range(n))
        expected = sorted(range(m+n), key=lambda i: ((x+y)[i],i))
        pairs = [(v,i) for i,v in enumerate(x+y)]
        assert [i for _,i in odd_even(pairs[:m],pairs[m:])] == expected
        for cube in (False,True):
            assert merge_ranks(x,y,2**rng.randrange(0,7),cube) == expected
            count += 1
    print(f'{count} root-merge oracle cases and exhaustive unequal Batcher zero-one checks passed')


if __name__ == '__main__': main()
