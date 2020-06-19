# RAPLCap - powercap

This implementation of the `raplcap` interface uses the [Linux Power Capping Framework](https://www.kernel.org/doc/Documentation/power/powercap/powercap.txt) abstractions in the Linux `/sys` filesystem.

## Prerequisites

The [powercap](https://github.com/powercap/powercap) library provides C bindings to RAPL in sysfs, including automatic discovery of available RAPL zones.
If using a Debian-based Linux distribution (e.g., Debian Sid or Ubuntu 18.04 LTS "Bionic Beaver" or newer), you may install the [libpowercap-dev](apt:libpowercap-dev) package instead of building the library from source.

To enable the required `intel-rapl` powercap control type, ensure that the appropriate kernel module is loaded:

```sh
sudo modprobe intel_rapl_msr
```

On on kernels older than 5.3:

```sh
sudo modprobe intel_rapl
```
