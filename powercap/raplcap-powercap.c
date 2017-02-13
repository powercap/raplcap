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

static void raplcap_limit_to_powercap(const raplcap_limit* l, uint64_t* us, uint64_t* uw) {
  assert(l != NULL);
  assert(us != NULL);
  assert(uw != NULL);
  static const uint64_t ONE_MILLION = 1000000;
  *us = ONE_MILLION * l->seconds;
  *uw = ONE_MILLION * l->watts;
}

static void powercap_to_raplcap(uint64_t us, uint64_t uw, raplcap_limit* l) {
  assert(l != NULL);
  static const double ONE_MILLION = 1000000.0;
  l->seconds = ((double) us) / ONE_MILLION;
  l->watts = ((double) uw) / ONE_MILLION;
}

static int raplcap_zone_to_powercap(raplcap_zone zone, powercap_rapl_zone* z) {
  assert(z != NULL);
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
      return -1;
  }
  return 0;
}

int raplcap_init(raplcap* rc) {
  powercap_rapl_pkg* pkgs;
  uint32_t i;
  int err_save;
  if (rc == NULL) {
    errno = EINVAL;
    return -1;
  }
  // get the number of packages/sockets
  rc->nsockets = powercap_rapl_get_num_packages();
  if (rc->nsockets == 0 || (pkgs = malloc(rc->nsockets * sizeof(powercap_rapl_pkg))) == NULL) {
    return -1;
  }
  rc->state = pkgs;
  for (i = 0; i < rc->nsockets; i++) {
    if (powercap_rapl_init(i, &pkgs[i], 0)) {
      err_save = errno;
      raplcap_destroy(rc);
      errno = err_save;
      return -1;
    }
  }
  return 0;
}

int raplcap_destroy(raplcap* rc) {
  uint32_t i;
  int err_save = 0;
  if (rc != NULL && rc->state != NULL) {
    for (i = 0; i < rc->nsockets; i++) {
      if (powercap_rapl_destroy(&((powercap_rapl_pkg*) rc->state)[i])) {
        err_save = errno;
      }
    }
    free(rc->state);
    rc->state = NULL;
    rc->nsockets = 0;
    errno = err_save;
  }
  return err_save ? -1 : 0;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  return rc == NULL ? powercap_rapl_get_num_packages() : rc->nsockets;
}

int raplcap_is_zone_supported(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  powercap_rapl_zone z;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_powercap(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  return powercap_rapl_is_zone_supported(&((powercap_rapl_pkg*) rc->state)[socket], z);
}

int raplcap_is_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  powercap_rapl_zone z;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_powercap(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  return powercap_rapl_is_enabled(&((powercap_rapl_pkg*) rc->state)[socket], z);
}

int raplcap_set_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone, int enabled) {
  powercap_rapl_zone z;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_powercap(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  return powercap_rapl_set_enabled(&((powercap_rapl_pkg*) rc->state)[socket], z, enabled);
}

int raplcap_get_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  uint64_t time_window, power_limit;
  powercap_rapl_zone z;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_powercap(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  const powercap_rapl_pkg* pkg = &((powercap_rapl_pkg*) rc->state)[socket];
  if (limit_long != NULL) {
    if (powercap_rapl_get_time_window_us(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, &time_window) ||
        powercap_rapl_get_power_limit_uw(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, &power_limit)) {
      return -1;
    }
    powercap_to_raplcap(time_window, power_limit, limit_long);
  }
  if (limit_short != NULL) {
    if (powercap_rapl_get_time_window_us(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT, &time_window) ||
        powercap_rapl_get_power_limit_uw(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT, &power_limit)) {
      return -1;
    }
    powercap_to_raplcap(time_window, power_limit, limit_short);
  }
  return 0;
}

int raplcap_set_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  uint64_t time_window, power_limit;
  powercap_rapl_zone z;
  if (rc == NULL || socket >= rc->nsockets || raplcap_zone_to_powercap(zone, &z)) {
    errno = EINVAL;
    return -1;
  }
  const powercap_rapl_pkg* pkg = &((powercap_rapl_pkg*) rc->state)[socket];
  if (limit_long != NULL) {
    raplcap_limit_to_powercap(limit_long, &time_window, &power_limit);
    if (time_window != 0 && powercap_rapl_set_time_window_us(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, time_window)) {
      return -1;
    }
    if (power_limit != 0 && powercap_rapl_set_power_limit_uw(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, power_limit)) {
      return -1;
    }
  }
  if (limit_short != NULL) {
    raplcap_limit_to_powercap(limit_short, &time_window, &power_limit);
    if (time_window != 0 && powercap_rapl_set_time_window_us(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT, time_window)) {
      return -1;
    }
    if (power_limit != 0 && powercap_rapl_set_power_limit_uw(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT, power_limit)) {
      return -1;
    }
  }
  return 0;
}
