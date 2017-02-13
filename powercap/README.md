# RAPLCap - powercap

This implementation of the `raplcap` interface uses the [Linux Power Capping Framework](https://www.kernel.org/doc/Documentation/power/powercap/powercap.txt) abstractions in the Linux `/sys` filesystem.

## Prerequisites

* [powercap](https://github.com/powercap/powercap) - provides C bindings to RAPL in sysfs, including automatic discovery of available RAPL zones.

To load the `intel_rapl` kernel module:

```sh
sudo modprobe intel_rapl
```
