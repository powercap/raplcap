# RAPLCap - libmsr

This implementation of the `raplcap` interface uses LLNL's [libmsr](https://software.llnl.gov/libmsr) library.
It is no longer documented as a normal backend for RAPLCap - the libmsr API is not stable and only supports a limited number of platforms.

## Prerequisites

* [libmsr](https://software.llnl.gov/libmsr) >= 2.1, most recently tested with release `v0.3.0`

When running `cmake`, you may need to set the property `CMAKE_PREFIX_PATH` to the install directory for `libmsr`, e.g.:

``` sh
cmake .. -DCMAKE_PREFIX_PATH=/path/to/libmsr/install_dir/
```

For instructions on enabling and configuring MSRs, see the README in [../msr](../msr).
