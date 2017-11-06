# RAPLCap - Intel&reg; Power Gadget

This implementation of the `raplcap` interface uses the [Intel&reg; Power Gadget](https://software.intel.com/en-us/articles/intel-power-gadget/).
It is mostly a proof-of-concept implementation and not documented as a normal backend for RAPLCap.

Note that because IPG is read-only, the RAPLCap setter functions are not supported in this implementation.
In fact, it can only read Package-level power limits.

## Prerequisites

* [Intel&reg; Power Gadget >= 2.7](https://software.intel.com/en-us/articles/intel-power-gadget/)

On Windows, download and install IPG from the above link.

On OSX, install IPG using:
```sh
brew cask install intel-power-gadget
```
