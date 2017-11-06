# RAPLCap

This project provides a C interface for getting/setting power caps with Intel Running Average Power Limit (RAPL).
It supports multiple implementations with different backends:

* `libraplcap-msr`: Uses [Model-Specific Register](https://en.wikipedia.org/wiki/Model-specific_register) files in the Linux `/dev` filesystem.
* `libraplcap-powercap`: Uses the [Linux Power Capping Framework](https://www.kernel.org/doc/Documentation/power/powercap/powercap.txt) abstractions in the Linux `/sys` filesystem.
* `libraplcap-libmsr`: Uses LLNL's [libmsr](https://software.llnl.gov/libmsr) interface.

It also provides binaries for getting/setting RAPL configurations from the command line.
Each provides the same command line interface, but use different RAPLCap library backends.

* `rapl-configure-msr`
* `rapl-configure-powercap`
* `rapl-configure-libmsr`


## Prerequisites

First, you must be using an Intel&reg; processor that supports RAPL - Sandy Bridge (2nd generation Intel&reg; Core) or newer.

Currently only Linux systems are supported.

This project optionally depends on:

* [powercap](https://github.com/powercap/powercap) - backend required to compile and run the `powercap` implementation.
* [libmsr](https://github.com/LLNL/libmsr/) (>= 2.1) - backend required to compile and run the `libmsr` implementation, most recently tested with release `v0.3.0`.

If dependencies are not found, CMake will not attempt to compile the implementations that use them.

Users are expected to be familiar with basic RAPL capabilities and terminology, like zones (domains) and long/short term power constraints.
Refer to Intel RAPL documentation for more technical information, especially the *Intel&reg; 64 and IA-32 Architectures Software Developer Manual, Volume 3: System Programming Guide.*

Due to lack of portability in backends and data availability on some systems, the interface does not support discovering processor min/max power caps or thermal design power.
Users should reference their hardware documentation or other system utilities to discover this information as needed.


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
pkg-config --libs --static raplcap-msr
pkg-config --libs --static raplcap-powercap
pkg-config --libs --static raplcap-libmsr
```

Or in your Makefile, add to your linker flags one of:

``` Makefile
$(shell pkg-config --libs --static raplcap-msr)
$(shell pkg-config --libs --static raplcap-powercap)
$(shell pkg-config --libs --static raplcap-libmsr)
```

You may leave off the `--static` option if you built shared object libraries.

Depending on your install location, you may also need to augment your compiler flags with one of:

``` sh
pkg-config --cflags raplcap-msr
pkg-config --cflags raplcap-powercap
pkg-config --cflags raplcap-libmsr
```


## Usage

See the man pages for the `rapl-configure` binaries, or run them with the `-h` or `--help` option for instructions.

The [raplcap.h](inc/raplcap.h) header provides the C interface along with detailed function documentation for using the libraries.

For backend-specific runtime dependencies, see the README files in their implementation subdirectories:

* [msr/README.md](msr/README.md)
* [powercap/README.md](powercap/README.md)
* [libmsr/README.md](libmsr/README.md)

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


## Project Source

Find this and related project sources at the [powercap organization on GitHub](https://github.com/powercap).  
This project originates at: https://github.com/powercap/raplcap

Bug reports and pull requests for new implementations, bug fixes, and enhancements are welcome.
