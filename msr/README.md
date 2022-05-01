# RAPLCap - msr

This implementation of the `raplcap` interface reads directly from Model-Specific Registers on Linux platforms.

It supports processors described beginning with the Sandy Bridge microarchitecture through the latest documented in the [Intel Software Developer's Manual](https://software.intel.com/en-us/articles/intel-sdm), Volume 4 as of April 2022.

To see if your processor is compatible, first check that the CPU is a `GenuineIntel`:

```sh
cat /proc/cpuinfo | grep vendor | uniq | awk '{print $3}'
```

Then check if the model is listed in [raplcap-cpuid.h](./raplcap-cpuid.h):

```sh
printf "0x%X\n" $(cat /proc/cpuinfo | grep model | grep -v name | uniq | cut -d: -f2)
```


## Prerequisites

The implementation first checks for the [msr-safe](https://github.com/LLNL/msr-safe) kernel module, otherwise it falls back on the `msr` kernel module.

To load the `msr` kernel module:

```sh
sudo modprobe msr
```

## Configuring msr-safe

If using the `msr-safe` module, ensure that your user has read/write privileges to the appropriate `msr_safe` file(s) located at `/dev/cpu/*/msr_safe` and that the following registers have read/write enabled:

* `MSR_RAPL_POWER_UNIT`
* `MSR_PKG_POWER_LIMIT`
* `MSR_PKG_ENERGY_STATUS`
* `MSR_PP0_POWER_LIMIT`
* `MSR_PP0_ENERGY_STATUS`
* `MSR_PP1_POWER_LIMIT`
* `MSR_PP1_ENERGY_STATUS`
* `MSR_DRAM_POWER_LIMIT`
* `MSR_DRAM_ENERGY_STATUS`
* `MSR_PLATFORM_POWER_LIMIT`
* `MSR_PLATFORM_ENERGY_COUNTER`
* `MSR_VR_CURRENT_CONFIG`

You can add these registers to the allowlist by running from this directory:

```sh
sudo sh -c 'cat etc/msr_safe_allowlist >> /dev/cpu/msr_allowlist'
```
