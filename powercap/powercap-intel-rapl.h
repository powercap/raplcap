/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * A simple interface for configuring RAPL through a powercap control type.
 * Note that not all RAPL zones support short_term constraints.
 * Unless otherwise stated, all functions return 0 on success or a negative value on error.
 *
 * Setter functions do not verify that written values are accepted by RAPL.
 * Users may want to add a debug option to their software that follows writes/sets with a read/get.
 *
 * These operations do basic I/O - it may reasonably be expected that callers need to handle I/O errors.
 * For example, it has been seen that "powercap_intel_rapl_get_max_power_uw" sets errno=ENODATA for power zones.
 *
 * Prior to Cascade Lake CPUs (2019), RAPL top-level instances mapped one-to-one with physical sockets/packages.
 * Some systems now support multiple die on a physical socket/package, resulting in multiple top-level instances per
 * physical socket/package.
 * It is also possible that the scope of a top-level instances could change again in the future.
 * Thus, it should not be assumed that a 'powercap_intel_rapl_parent' instance maps one-to-one with a physical socket.
 * Intel's backward compatibility _appears_ to be in a zone's name, but even this is not explicitly guaranteed - it is
 * the user's responsibility to interpret what a top-level RAPL instance actually is.
 *
 * @author Connor Imes
 * @date 2016-05-12
 */
#ifndef _POWERCAP_INTEL_RAPL_H_
#define _POWERCAP_INTEL_RAPL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <unistd.h>
#include <powercap.h>

#include "raplcap.h"

#pragma GCC visibility push(hidden)

/**
 * Files for each zone.
 */
typedef struct powercap_intel_rapl_zone_files {
  powercap_zone zone;
  powercap_constraint constraints[RAPLCAP_CONSTRAINT_PEAK_POWER + 1];
} powercap_intel_rapl_zone_files;

/**
 * All files for a top-level RAPL instance.
 */
typedef struct powercap_intel_rapl_parent {
  powercap_intel_rapl_zone_files zones[RAPLCAP_ZONE_PSYS + 1];
} powercap_intel_rapl_parent;

/**
 * Get the number of top-level (parent) RAPL instances found.
 * Returns 0 and sets errno if none are found.
 */
uint32_t powercap_intel_rapl_get_num_instances(void);

/**
 * Initialize the struct for the parent zone with the given identifier.
 * Read-only access can be requested, which may prevent the need for elevated privileges.
 */
int powercap_intel_rapl_init(uint32_t id, powercap_intel_rapl_parent* parent, int read_only);

/**
 * Clean up file descriptors.
 */
int powercap_intel_rapl_destroy(powercap_intel_rapl_parent* parent);

/**
 * Check if a zone is supported.
 * The uncore power zone is usually only available on client-side hardware.
 * The DRAM power zone is usually only available on server-side hardware.
 * Some systems may expose zones like DRAM without actually supporting power caps for them.
 * The PSys power zone may be available on Skylake processors and later.
 * Returns 1 if supported, 0 if unsupported.
 */
int powercap_intel_rapl_is_zone_supported(const powercap_intel_rapl_parent* parent, raplcap_zone zone);

/**
 * Check if a constraint is supported for a zone.
 * Returns 1 if supported, 0 if unsupported.
 */
int powercap_intel_rapl_is_constraint_supported(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint);

/**
 * Get the zone name.
 * Returns a non-negative value for the number of bytes read, a negative value in case of error.
 */
ssize_t powercap_intel_rapl_get_name(const powercap_intel_rapl_parent* parent, raplcap_zone zone, char* buf, size_t size);

/**
 * Check if zone is enabled.
 * Returns 1 if enabled, 0 if disabled, a negative value in case of error.
 */
int powercap_intel_rapl_is_enabled(const powercap_intel_rapl_parent* parent, raplcap_zone zone);

/**
 * Enable/disable a zone.
 */
int powercap_intel_rapl_set_enabled(const powercap_intel_rapl_parent* parent, raplcap_zone zone, int enabled);

/**
 * Get the max energy range in microjoules.
 */
int powercap_intel_rapl_get_max_energy_range_uj(const powercap_intel_rapl_parent* parent, raplcap_zone zone, uint64_t* val);

/**
 * Get the current energy in microjoules.
 */
int powercap_intel_rapl_get_energy_uj(const powercap_intel_rapl_parent* parent, raplcap_zone zone, uint64_t* val);

/**
 * Get the power limit in microwatts.
 */
int powercap_intel_rapl_get_power_limit_uw(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint, uint64_t* val);

/**
 * Set the power limit in microwatts.
 */
int powercap_intel_rapl_set_power_limit_uw(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint, uint64_t val);

/**
 * Get the time window in microseconds.
 */
int powercap_intel_rapl_get_time_window_us(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint, uint64_t* val);

/**
 * Set the time window in microseconds.
 */
int powercap_intel_rapl_set_time_window_us(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint, uint64_t val);

#pragma GCC visibility pop

#ifdef __cplusplus
}
#endif

#endif
