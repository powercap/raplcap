# Release Notes

## [Unreleased]

### Added

* Interface function 'raplcap_get_num_die'
* [msr] Support for Ice Lake, Models 0x7D, 0x7E, 0x6A, 0x6C
* [msr] Support for Comet Lake, Models 0xA5, 0xA6
* [rapl-configure] New -N/--ndie flag
* [rapl-configure] Long option --npackages (supersedes --nsockets)
* [rapl-configure] Long option --package (supersedes --socket)

### Changed

* Increased minimum CMake version from 2.8.5 to 2.8.12 to support target_compile_definitions
* [msr] Updated for the Intel Software Developer's Manual, May 2020 release
* [powercap] Library dependency now requires version >= 0.3.0

### Deprecated

* [rapl-configure] Long option --nsockets, use --npackages instead
* [rapl-configure] Long option --socket, use --package instead

### Removed

* libmsr implementation - the upstream library is no longer being developed


### Fixed

* [powercap] Support for PSYS zones ([#8])


## [v0.4.0] - 2019-12-14

### Added

* [msr] Support for Atom Tremont ("Jacobsville"), Model 0x86
* [msr] Functions for clamping: 'raplcap_msr_is_zone_clamped' and 'raplcap_msr_set_zone_clamped'
* [msr] Functions for locking: 'raplcap_msr_is_zone_locked' and 'raplcap_msr_set_zone_locked'
* [powercap] Support for new "package-X-die-Y" Package zone naming convention in sysfs
* [powercap] Support for systems with more than 10 packages or dies
* [rapl-configure] MSR-only support for locked bit status
* [rapl-configure] MSR-only support for clamping bit status

### Changed

* README: Enable zones in example code snippet ([#7])
* [msr] Updated for the Intel Software Developer's Manual, May 2019 release
* [rapl-configure] Use consistent output formatting for all zones

### Fixed

* [msr] Fixed [#6]: Use correct power limit mask (credit: Marc Girard)


## [v0.3.0] - 2018-06-01

### Added

* MSR implementation support for Atom processors
* MSR implementation support for Cannon Lake mobile processors
* Functions for reading energy counters (also added output to rapl-configure binaries)
* MSR implementation functions to expose time, power, and energy units
* New -e/--enabled flag for rapl-configure binaries
* More units tests

### Changed

* MSR implementation will now only compile on x86 (required for RAPL anyway)
* Cannot disable zones by setting time window or power limit values to 0 in rapl-configure binaries (use -e/--enabled=0 instead)
* Improved runtime checking for short term constraint support

### Fixed

* Some server processors have DRAM energy units distinct from those specified in the power unit register


## [v0.2.0] - 2017-12-11

### Added

* Some high-level documentation on RAPL and power capping
* Simple unit and integration tests

### Changed

* API breakage: Normalized API functions so RAPLCap context is always the first parameter
* MSR discovery now uses sysfs files instead of parsing /proc/cpuinfo
* Consistently reset socket count to 0 on destroy or initialization failure
* Clamping messages reduced from warning to informational
* Silence error messages when errors are anticipated and handled

### Deprecated

* The libmsr implementation is no longer a primary RAPLCap backend and may be removed in the future

### Fixed

* Enable zones even if clamping can't be set (not available for all zones/platforms)
* Fixed "raplcap_is_zone_supported" in MSR implementation
* Only operate on short term constraints in powercap implementation if zone supports it


## [v0.1.2] - 2017-11-12

### Added

* VERSION and SOVERSION to shared object libraries
* Multiarch support (use GNU standard installation directories)

### Changed

* Increased minimum CMake version from 2.8 to 2.8.5 to support GNUInstallDirs
* Updated license to use author as copyright holder
* Expand man page for rapl-configure binaries
* Code optimizations

### Fixed

* Sort packages in raplcap-powercap as sysfs entries may be out of order
* Minor fixes to some log messages


## [v0.1.1] - 2017-10-02

### Added

* Added man page for rapl-configure binaries

### Changed

* Upstream libmsr removed functionality for PP0/PP1 (core and uncore) zones, so we check for functions during build
* Try to open files as read-only when possible, but no guarantees
* Build improvements

### Fixed

* Fixed possible crash with getopt in rapl-configure binaries


## v0.1.0 - 2017-06-09

### Added

* Initial public release


[Unreleased]: https://github.com/powercap/raplcap/compare/v0.4.0...HEAD
[v0.4.0]: https://github.com/powercap/raplcap/compare/v0.3.0...v0.4.0
[v0.3.0]: https://github.com/powercap/raplcap/compare/v0.2.0...v0.3.0
[v0.2.0]: https://github.com/powercap/raplcap/compare/v0.1.2...v0.2.0
[v0.1.2]: https://github.com/powercap/raplcap/compare/v0.1.1...v0.1.2
[v0.1.1]: https://github.com/powercap/raplcap/compare/v0.1.0...v0.1.1
[#6]: https://github.com/powercap/raplcap/issues/6
[#7]: https://github.com/powercap/raplcap/issues/7
[#8]: https://github.com/powercap/raplcap/issues/8
