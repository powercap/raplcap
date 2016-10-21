/**
 * Implementation that wraps libpowercap with RAPL sysfs interface.
 *
 * @author Connor Imes
 * @date 2016-05-13
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "raplcap.h"
// powercap header
#include <powercap-rapl.h>

static inline uint64_t to_micro(double val) {
  static const uint64_t ONE_MILLION = 1000000;
  return ONE_MILLION * val;
}

static inline double from_micro(uint64_t val) {
  static const double ONE_MILLION = 1000000.0;
  return ((double) val) / ONE_MILLION;
}

static inline void raplcap_limit_to_sysfs(const raplcap_limit* l, uint64_t* us, uint64_t* uw) {
  assert(l != NULL);
  assert(us != NULL);
  assert(uw != NULL);
  *us = to_micro(l->seconds);
  *uw = to_micro(l->watts);
}

static inline void sysfs_to_raplcap(uint64_t us, uint64_t uw, raplcap_limit* l) {
  assert(l != NULL);
  l->seconds = from_micro(us);
  l->watts = from_micro(uw);
}

static int raplcap_zone_to_sysfs(raplcap_zone zone, powercap_rapl_zone* z) {
  assert(z != NULL);
  int ret = 0;
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      *z = POWERCAP_RAPL_ZONE_PACKAGE;
      break;
    case RAPLCAP_ZONE_CORE:
      *z =  POWERCAP_RAPL_ZONE_CORE;
      break;
    case RAPLCAP_ZONE_UNCORE:
      *z =  POWERCAP_RAPL_ZONE_UNCORE;
      break;
    case RAPLCAP_ZONE_DRAM:
      *z =  POWERCAP_RAPL_ZONE_DRAM;
      break;
    case RAPLCAP_ZONE_PSYS:
      *z =  POWERCAP_RAPL_ZONE_PSYS;
      break;
    default:
      ret = -1;
      break;
  }
  return ret;
}

int raplcap_init(raplcap* rc) {
  uint32_t i, j, npackages;
  powercap_rapl_pkg* pkgs;
  int err_save = 0;
  if (rc == NULL) {
    errno = EINVAL;
    return -1;
  }
  // zero-out values
  memset(rc, 0, sizeof(raplcap));
  // get the number of packages/sockets
  npackages = powercap_rapl_get_num_packages();
  if (npackages == 0) {
    // no packages found - cannot proceed
    return -1;
  }
  pkgs = malloc(npackages * sizeof(powercap_rapl_pkg));
  if (pkgs == NULL) {
    return -1;
  }
  for (i = 0; i < npackages; i++) {
    if (powercap_rapl_init(i, &pkgs[i], 0)) {
      err_save = errno;
      // failed initialization - cleanup and return error
      for (j = 0; j < i; j++) {
        powercap_rapl_destroy(&pkgs[j]);
      }
      free(pkgs);
      errno = err_save;
      return -1;
    }
  }
  rc->state = pkgs;
  rc->nsockets = npackages;
  return 0;
}

int raplcap_destroy(raplcap* rc) {
  powercap_rapl_pkg* pkgs;
  uint32_t i;
  int err_save = 0;
  if (rc == NULL || rc->state == NULL) {
    errno = EINVAL;
    return -1;
  }
  pkgs = (powercap_rapl_pkg*) rc->state;
  for (i = 0; i < rc->nsockets; i++) {
    if (powercap_rapl_destroy(&pkgs[i])) {
      err_save = errno;
    }
  }
  rc->state = NULL;
  rc->nsockets = 0;
  errno = err_save;
  return err_save ? -1 : 0;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  return rc == NULL ? powercap_rapl_get_num_packages() : rc->nsockets;
}

int raplcap_is_zone_supported(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  powercap_rapl_zone z = POWERCAP_RAPL_ZONE_PACKAGE;
  const powercap_rapl_pkg* pkgs;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_sysfs(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  pkgs = (powercap_rapl_pkg*) rc->state;
  return powercap_rapl_is_zone_supported(&pkgs[socket], z);
}

int raplcap_is_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  powercap_rapl_zone z = POWERCAP_RAPL_ZONE_PACKAGE;
  const powercap_rapl_pkg* pkg;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_sysfs(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  pkg = &((powercap_rapl_pkg*) rc->state)[socket];
  return powercap_rapl_is_enabled(pkg, z);
}

int raplcap_set_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone, int enabled) {
  powercap_rapl_zone z = POWERCAP_RAPL_ZONE_PACKAGE;
  const powercap_rapl_pkg* pkg;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_sysfs(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  pkg = &((powercap_rapl_pkg*) rc->state)[socket];
  return powercap_rapl_set_enabled(pkg, z, enabled);
}

int raplcap_get_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  uint64_t time_window, power_limit;
  int ret = 0;
  powercap_rapl_zone z = POWERCAP_RAPL_ZONE_PACKAGE;
  const powercap_rapl_pkg* pkg;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_sysfs(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  pkg = &((powercap_rapl_pkg*) rc->state)[socket];
  if (limit_long != NULL) {
    if (powercap_rapl_get_time_window_us(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, &time_window) ||
        powercap_rapl_get_power_limit_uw(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, &power_limit)) {
      ret = -1;
    } else {
      sysfs_to_raplcap(time_window, power_limit, limit_long);
    }
  }
  if (limit_short != NULL) {
    if (powercap_rapl_get_time_window_us(pkg, zone, POWERCAP_RAPL_CONSTRAINT_SHORT, &time_window) ||
        powercap_rapl_get_power_limit_uw(pkg, zone, POWERCAP_RAPL_CONSTRAINT_SHORT, &power_limit)) {
      ret = -1;
    } else {
      sysfs_to_raplcap(time_window, power_limit, limit_short);
    }
  }
  return ret;
}

int raplcap_set_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  uint64_t time_window, power_limit;
  powercap_rapl_zone z = POWERCAP_RAPL_ZONE_PACKAGE;
  const powercap_rapl_pkg* pkg;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_sysfs(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  pkg = &((powercap_rapl_pkg*) rc->state)[socket];
  if (limit_long != NULL) {
    raplcap_limit_to_sysfs(limit_long, &time_window, &power_limit);
    if (time_window != 0 && powercap_rapl_set_time_window_us(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, time_window)) {
      return -1;
    }
    if (power_limit != 0 && powercap_rapl_set_power_limit_uw(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, power_limit)) {
      return -1;
    }
  }
  if (limit_short != NULL) {
    raplcap_limit_to_sysfs(limit_short, &time_window, &power_limit);
    if (time_window != 0 && powercap_rapl_set_time_window_us(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT, time_window)) {
      return -1;
    }
    if (power_limit != 0 && powercap_rapl_set_power_limit_uw(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT, power_limit)) {
      return -1;
    }
  }
  return 0;
}
