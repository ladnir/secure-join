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


def unary_count(bits):
    result = 0
    for i, bit in enumerate(bits):
        if bit ^ (bits[i+1] if i+1 < len(bits) else 0):
            result ^= i+1
    return result


def merge_ranks(x, y, block, cube, optimized=False):
    m, n = len(x), len(y)
    blocks = [y[i:i+block] for i in range(0, n, block)]
    # The last block also receives every X greater than all of Y.
    target = [bisect.bisect_left([v[-1] for v in blocks[:-1]], value) for value in x]
    first = [i == 0 or target[i] != target[i-1] for i in range(m)]
    if optimized == 2:
        maps = [[int(t == j) for j in range(len(blocks))] for t in target]
        first_maps = [[v & (1 ^ maps[i-1][j]) if i else v for j,v in enumerate(row)] for i,row in enumerate(maps)]
        assert [bool(sum(row)) for row in first_maps] == first
    tags = [0] * (len(blocks) + m)
    for i in range(m):
        tags[target[i] if first[i] else len(blocks)+i] = i+1
    assert sorted(tags) == [0]*len(blocks) + list(range(1,m+1))
    selected = [blocks[target[i]] if first[i] else [] for i in range(m)]
    coarse = [bisect.bisect_right(x, v[-1]) for v in blocks]
    if optimized:
        assert coarse == [unary_count([int(a <= v[-1]) for a in x]) for v in blocks]
    xr = [0]*m
    delta = [[0]*len(v) for v in blocks]
    if cube:
        extracted = [value for v in selected for value in v]
        groups = 0
        for i in range(m):
            groups += first[i]
            r = sum(value < x[i] for value in extracted)
            if optimized:
                offsets = [unary_count([int(value < x[i]) for value in v] + [0]*(block-len(v))) for v in selected]
                low = 0
                for offset in offsets:
                    low ^= offset & (block-1)
                assert r == low + block * sum(offset == block for offset in offsets)
                if optimized == 2:
                    # All real blocks after slot i are above X_i. Dummy slots
                    # contain only infinity. The upper triangular matrix is zero.
                    assert offsets[i+1:] == [0]*(m-i-1)
                    own_full = (sum(offset == block for offset in offsets[:i+1]) ^ groups ^ 1) & 1
                    within = low | (own_full * block)
                    assert within == bisect.bisect_left(blocks[target[i]], x[i])
                    high = target[i] ^ (((len(blocks)-1) ^ len(blocks)) if own_full else 0)
                    assert high*block + (within & (block-1)) == target[i]*block + within
            xr[i] = i + target[i]*block + r - (groups-1)*block
        for i in range(m):
            if first[i]:
                delta[target[i]] = [(unary_count([int(a <= value) for a in x]) ^ coarse[target[i]])
                                   if optimized else bisect.bisect_right(x, value)-coarse[target[i]] for value in selected[i]]
    else:
        copied = []
        for i in range(m):
            copied.append(selected[i] if first[i] else copied[-1])
            offset = unary_count([int(value < x[i]) for value in copied[i]]) if optimized else bisect.bisect_left(copied[i], x[i])
            xr[i] = i + target[i]*block + offset
        suffix = [[int(x[i] <= value) for value in copied[i]] for i in range(m)]
        if optimized:
            for i in range(m-1, -1, -1):
                suffix[i] = [suffix[i+1][t] if a and i+1 < m and not first[i+1] else i+a
                             for t, a in enumerate(suffix[i])]
        else:
            for i in range(m-2, -1, -1):
                if not first[i+1]: suffix[i] = [a+b for a,b in zip(suffix[i],suffix[i+1])]
        for i in range(m):
            if first[i]: delta[target[i]] = [(v ^ coarse[target[i]]) if optimized else v+i-coarse[target[i]] for v in suffix[i]]
    yr = [j*block+t+((coarse[j] ^ delta[j][t]) if optimized else coarse[j]+delta[j][t]) for j in range(len(blocks)) for t in range(len(blocks[j]))]
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
                            for optimized in (False,True,2):
                                assert merge_ranks(x,y,block,cube,optimized) == expected
                                count += 1
    rng = random.Random(23917)
    for _ in range(2000):
        m,n = rng.randrange(1,65),rng.randrange(1,257)
        x,y = sorted(rng.randrange(32) for _ in range(m)), sorted(rng.randrange(32) for _ in range(n))
        expected = sorted(range(m+n), key=lambda i: ((x+y)[i],i))
        pairs = [(v,i) for i,v in enumerate(x+y)]
        assert [i for _,i in odd_even(pairs[:m],pairs[m:])] == expected
        for cube in (False,True):
            block = 2**rng.randrange(0,7)
            for optimized in (False,True,2):
                assert merge_ranks(x,y,block,cube,optimized) == expected
                count += 1
    # Check the linear map on random XOR shares, including zero/full counts.
    for size in range(1,258):
        for length in range(size+1):
            plain = [int(i < length) for i in range(size)]
            share = [rng.randrange(2) for _ in range(size)]
            other = [a^b for a,b in zip(plain,share)]
            assert unary_count(share) ^ unary_count(other) == length
    print(f'{count} root-merge oracle cases and exhaustive unequal Batcher zero-one checks passed')


if __name__ == '__main__': main()
