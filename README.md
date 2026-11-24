# Secure Join

Secure Join implements oblivious two party database joins on secret shared table. It include the permutation protocols of [iacr/2024/547](https://eprint.iacr.org/2024/547) and the Alternating Moduli secret shared PRF protocols of [iacr/2024/582](https://eprint.iacr.org/2024/582).

### Build

The library can be cloned and built with networking support as
```
git clone https://github.com/Visa-Research/secure-join.git
cd SecureJoin
python3 build.py 
```

The output library `secure-join` and executable `frontend` will be written to `out/build/<platform>/`. The `frontend` can perform PSI based on files as input sets and communicate via sockets. See the output of `frontend` for details. 

### Correlation modes

Both peers select the noise mode in the final argument of `CorGenerator::init`. The default, `oc::SdNoiseDistribution::Regular`, samples fresh PPRF noise for each batch and shares one immutable full SPIN map per padded size within a process. Each worker owns its scratch.

With `oc::SdNoiseDistribution::Stationary`, use the same logical worker count and batch size on both peers. Each worker keeps its own bank per padded size, and each stream refreshes its active code every round. Banked SPIN uses the heuristic setup. Reusing the generator across joins retains public banks and starts a fresh private session. To benchmark a join with stationary noise, run `secJoinfrontend -bench -join -nn 12 -nt 2 -mock 0 -stationary`.

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

