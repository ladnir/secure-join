# Secure Join

Secure Join implements oblivious two party database joins on secret shared table. It include the permutation protocols of [iacr/2024/547](https://eprint.iacr.org/2024/547) and the Alternating Moduli secret shared PRF protocols of [iacr/2024/582](https://eprint.iacr.org/2024/582).

### Join performance

The full join went from **2.360 s in v1.0 (Tungsten) to 0.794 s in v1.1 (stationary SPIN)** (2.97× speedup). Total communication fell from **43,870 to 8,018 bytes per input row** (82% less). Four-bit radix passes halve the number of sorting passes, and stationary SPIN cuts preprocessing communication by reusing correlation setup across batches.

Both times are medians of 12 runs on an AMD Ryzen 9 7950X with 16,384 rows per party, four workers per party, 32,768 correlations per batch, local sockets, and real base OTs. The v1.0 baseline was commit `7211d17`.

### Build

The library can be cloned and built with networking support as
```
git clone https://github.com/Visa-Research/secure-join.git
cd SecureJoin
python3 build.py 
```

The output library `secure-join` and executable `frontend` will be written to `out/build/<platform>/`. The `frontend` can perform PSI based on files as input sets and communicate via sockets. See the output of `frontend` for details. 

### Correlation modes

`gen.init(std::move(socket), prng, party, workers)` selects stationary noise with 32,768 correlations per batch. Each worker keeps its own SPIN bank per padded size, and each stream refreshes its active code every round. Banked SPIN uses the heuristic setup. Reusing the generator across joins retains the public banks and starts a fresh private session.

Pass `oc::SdNoiseDistribution::Regular` as the fifth argument for regular noise with 1,048,576 correlations per batch. This mode samples fresh PPRF noise for each batch and shares one immutable full SPIN map per padded size within a process. Each worker owns its scratch.

To override the batch size, pass `batchSize, mock, noise` after `workers`. Both peers must use the same mode and batch size. Stationary mode also requires the same worker count on both peers.

Run `secJoinfrontend -bench -join -nn 12 -nt 2 -mock 0` for the stationary default. Add `-regular` for the regular default. In either mode, `-b k` sets the batch size to 2^k correlations.

### Alternating-moduli PRF

For a 512-bit input $X$, the two-key construction computes $B\rho(A(X \mathbin{\&} k) + k')$.
The binary mask $k$ has 512 bits. The independent offset $k'$ has 256 uniform trits and stays fixed across evaluations with that key. Addition is over $\mathbb F_3$, $\rho$ maps $(0,1,2)$ to $(0,1,0)$ coordinatewise, and $B$ acts over $\mathbb F_2$.

Use `AltModPrf::KeyType::sample(prng)` to generate both components. An existing binary mask with an all-zero offset reproduces the v1.1 function. The 128-bit join interface expands its input first. The clear `evalExpanded` and two-party `evaluateExpanded` interfaces take 512-bit inputs directly in bit-sliced form. In shared-key mode the parties XOR-share $k$ and additively share $k'$ modulo three. Each party adds its offset share locally before the modulus switch, with no additional OTs or messages.

### Installing

The library and any fetched dependancies can be installed. 
```
python3 build.py --install
```
or 
```
python3 build.py --install=install/prefix/path
```
if a custom install prefix is perfered. Install can also be performed via cmake.

### Dependancy Managment

By default the depenancies are fetched automaticly. This can be turned off by using cmake directly or adding `-D FETCH_AUTO=OFF`. For other options see the cmake output or that of `python build.py --help`.

If the dependancie is installed to the system, then cmake should automaticly find it. If they are installed to a specific location, then you call tell cmake about them as 
```
python3 build.py -D CMAKE_PREFIX_PATH=install/prefix/path
```

