# RAPLCap - msr

This implementation of the `raplcap` interface reads directly from Model-Specific Registers on Linux platforms.

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
* `MSR_PP0_POWER_LIMIT`
* `MSR_PP1_POWER_LIMIT`
* `MSR_DRAM_POWER_LIMIT`
* `MSR_PLATFORM_POWER_LIMIT`

You can add these registers to the whitelist by running from this directory:

```sh
sudo sh -c 'cat etc/msr_safe_whitelist >> /dev/cpu/msr_whitelist'
```
