# Release Notes

## Unreleased

 * Added VERSION and SOVERSION to shared object libraries
 * Updated license to use author as copyright holder
 * Use GNU standard installation directories (multiarch support)
 * Increased minimum CMake version to 2.8.5 to support GNUInstallDirs

## v0.1.1 - 2017-10-02

 * Added man page for rapl-configure binaries
 * Fixed possible crash with getopt in rapl-configure binaries
 * Upstream libmsr removed functionality for PP0/PP1 (core and uncore) zones, so we check for functions during build
 * Try to open files as read-only when possible, but no guarantees
 * Build improvements

## v0.1.0 - 2017-06-09

 * Initial public release
