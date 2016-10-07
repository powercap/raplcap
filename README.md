# RAPLCap

This project provides a C interface for getting/setting power caps with Intel Running Average Power Limit (RAPL).
It supports multiple implementations.

It also provides two binaries, `rapl-configure-msr` and `rapl-configure-sysfs`, for getting/setting RAPL configurations from the command line.
Each provides the same command line interface, but use different RAPLCap libraries for interfacing with RAPL.

## Prerequisites

This project depends on:
 * [libmsr](https://github.com/LLNL/libmsr/)
 * [powercap](https://github.com/powercap/powercap)

## Building

This project uses CMake.

To build all libraries, run:

``` sh
mkdir _build
cd _build
cmake ..
make
```

When running `cmake`, you may need to set the property `CMAKE_PREFIX_PATH` to the install directory for `libmsr`, e.g.:

``` sh
cmake .. -DCMAKE_PREFIX_PATH=/path/to/libmsr/install_dir/
```

## Installing

To install all libraries and headers, run with proper privileges:

``` sh
make install
```

On Linux, installation typically places libraries in `/usr/local/lib` and header files in `/usr/local/include`.

## Uninstalling

Install must be run before uninstalling in order to have a manifest.

To remove libraries and headers installed to the system, run with proper privileges:

``` sh
make uninstall
```

## Linking

To link with an implementation of RAPLCap, get linker information (including transitive dependencies) with `pkg-config`, e.g. one of:

``` sh
pkg-config --libs --static raplcap-sysfs
pkg-config --libs --static raplcap-msr
```

Or in your Makefile, add to your linker flags one of:

``` Makefile
$(shell pkg-config --libs --static raplcap-sysfs)
$(shell pkg-config --libs --static raplcap-msr)
```

You may leave off the `--static` option if you built shared object libraries.

Depending on your install location, you may also need to augment your compiler flags with one of:

``` sh
pkg-config --cflags raplcap-sysfs
pkg-config --cflags raplcap-msr
```

## Usage

The following is a simple example of setting power caps.

``` C
  raplcap rc;
  raplcap_limit rl_short, rl_long;
  uint32_t i, n;

  // get the number of RAPL packages/sockets
  n = raplcap_get_num_sockets(NULL);
  if (n == 0) {
    perror("raplcap_get_num_sockets");
    return -1;
  }

  // initialize
  if (raplcap_init(&rc)) {
    perror("raplcap_init");
    return -1;
  }

  // for each socket, set a powercap of 100 Watts for short_term and 50 Watts for long_term constraints
  // a time window of 0 leaves the time window unchanged
  rl_short.watts = 100.0;
  rl_short.seconds = 0.0;
  rl_long.watts = 50.0;
  rl_long.seconds = 0.0;
  for (i = 0; i < n; i++) {
    if (raplcap_set_limits(i, &rc, RAPLCAP_ZONE_PACKAGE, &rl_long, &rl_short)) {
      perror("raplcap_set_limits");
    }
  }

  // cleanup
  if (raplcap_destroy(&rc)) {
    perror("raplcap_destroy");
  }
```
