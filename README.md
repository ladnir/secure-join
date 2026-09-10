# Secure Join

The **shuffled quicksort** baseline now supports stable duplicate handling,
batched comparisons, preprocessed correlation reserves, and an optional
three-pivot mode. See [docs/shuffled-quicksort.md](docs/shuffled-quicksort.md).
Development and correctness checks passed; formal quicksort paper benchmarks
have not been run.

The paper's recursive **Pi-median merge** is implemented with configurable
per-level parameters, real two-party cryptography, and a public cost planner.
See [docs/pi-median.md](docs/pi-median.md). Formal Pi-median paper benchmarks
have not been run.

The experimental two-party semi-honest **Pi-logstar merge** implementation and
its real-crypto benchmark are described in [docs/pi-logstar.md](docs/pi-logstar.md).
Measured scaling, parameter choices, and network results are in
[docs/logstar-results.md](docs/logstar-results.md).
The latest size-independent optimizations and matched Batcher comparison are
documented in [the development audit](docs/logstar-optimization-audit.md).
The paper benchmarks have not been rerun for these changes.

Secure Join implements oblivious two party database joins on secret shared table. It include the permutation protocols of [iacr/2024/547](https://eprint.iacr.org/2024/547) and the Alternating Moduli secret shared PRF protocols of [iacr/2024/582](https://eprint.iacr.org/2024/582).

### Build

The library can be cloned and built with networking support as
```
git clone https://github.com/Visa-Research/secure-join.git
cd SecureJoin
python3 build.py 
```

The output library `secure-join` and executable `frontend` will be written to `out/build/<platform>/`. The `frontend` can perform PSI based on files as input sets and communicate via sockets. See the output of `frontend` for details. 

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

