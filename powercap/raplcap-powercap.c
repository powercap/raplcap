/**
 * Implementation that wraps libpowercap with RAPL sysfs interface.
 *
 * @author Connor Imes
 * @date 2016-05-13
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "raplcap.h"
#define RAPLCAP_IMPL "raplcap-powercap"
#include "raplcap-common.h"
// powercap header
#include <powercap-rapl.h>

#define MAX_PKG_NAME_SIZE 16

#define HAS_SHORT_TERM(zone) (zone == RAPLCAP_ZONE_PACKAGE || zone == RAPLCAP_ZONE_PSYS)

static raplcap rc_default;

static powercap_rapl_pkg* get_pkg_zone(const raplcap* rc, uint32_t socket, raplcap_zone zone, powercap_rapl_zone* z) {
  assert(z != NULL);
  static const powercap_rapl_zone POWERCAP_RAPL_ZONES[] = {
    POWERCAP_RAPL_ZONE_PACKAGE, // RAPLCAP_ZONE_PACKAGE
    POWERCAP_RAPL_ZONE_CORE,    // RAPLCAP_ZONE_CORE
    POWERCAP_RAPL_ZONE_UNCORE,  // RAPLCAP_ZONE_UNCORE
    POWERCAP_RAPL_ZONE_DRAM,    // RAPLCAP_ZONE_DRAM
    POWERCAP_RAPL_ZONE_PSYS     // RAPLCAP_ZONE_PSYS
  };
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->nsockets == 0 || rc->state == NULL) {
    // unfortunately can't detect if the context just contains garbage
    raplcap_log(ERROR, "get_pkg_zone: Context is not initialized\n");
    errno = EINVAL;
    return NULL;
  }
  if (socket >= rc->nsockets) {
    raplcap_log(ERROR, "get_pkg_zone: Socket %"PRIu32" not in range [0, %"PRIu32")\n", socket, rc->nsockets);
    errno = EINVAL;
    return NULL;
  }
  if ((int) zone < 0 || (int) zone > RAPLCAP_ZONE_PSYS) {
    raplcap_log(ERROR, "get_pkg_zone: Unknown zone: %d\n", zone);
    errno = EINVAL;
    return NULL;
  }
  *z = POWERCAP_RAPL_ZONES[zone];
  return &((powercap_rapl_pkg*) rc->state)[socket];
}

static uint32_t get_powercap_sockets(void) {
  uint32_t sockets = powercap_rapl_get_num_packages();
  if (sockets == 0) {
    raplcap_perror(ERROR, "get_powercap_sockets: powercap_rapl_get_num_packages");
  }
  raplcap_log(DEBUG, "get_powercap_sockets: sockets=%"PRIu32"\n", sockets);
  return sockets;
}

static int sort_pkgs(const void* a, const void* b) {
  char name_a[MAX_PKG_NAME_SIZE] = { 0 };
  char name_b[MAX_PKG_NAME_SIZE] = { 0 };
  int ret;
  if (powercap_rapl_get_name((const powercap_rapl_pkg*) a, POWERCAP_RAPL_ZONE_PACKAGE, name_a, sizeof(name_a)) >= 0 &&
      powercap_rapl_get_name((const powercap_rapl_pkg*) b, POWERCAP_RAPL_ZONE_PACKAGE, name_b, sizeof(name_b)) >= 0) {
    // assumes names are in the form "package-N" and 0 <= N < 10 (N >= 10 would need more advanced parsing)
    if ((ret = strncmp(name_a, name_b, sizeof(name_a))) > 0) {
      raplcap_log(DEBUG, "sort_pkgs: Packages are out of order\n");
    }
  } else {
    raplcap_perror(ERROR, "sort_pkgs: powercap_rapl_get_name");
    ret = 0;
  }
  return ret;
}

int raplcap_init(raplcap* rc) {
  powercap_rapl_pkg* pkgs;
  uint32_t i;
  int err_save;
  const char* env_ro = getenv(ENV_RAPLCAP_READ_ONLY);
  int ro = env_ro == NULL ? 0 : atoi(env_ro);
  if (rc == NULL) {
    rc = &rc_default;
  }
  // get the number of packages/sockets
  if ((rc->nsockets = get_powercap_sockets()) == 0) {
    return -1;
  }
  if ((pkgs = malloc(rc->nsockets * sizeof(powercap_rapl_pkg))) == NULL) {
    raplcap_perror(ERROR, "raplcap_init: malloc");
    rc->nsockets = 0;
    return -1;
  }
  rc->state = pkgs;
  for (i = 0; i < rc->nsockets; i++) {
    if (powercap_rapl_init(i, &pkgs[i], ro)) {
      raplcap_perror(ERROR, "raplcap_init: powercap_rapl_init");
      err_save = errno;
      raplcap_destroy(rc);
      errno = err_save;
      return -1;
    }
  }
  // it's been observed that packages in sysfs may be numbered out of order; we must sort by name
  errno = 0;
  qsort(pkgs, rc->nsockets, sizeof(powercap_rapl_pkg), sort_pkgs);
  if (errno) {
    raplcap_log(ERROR, "raplcap_init: Failed to sort packages by name\n");
    err_save = errno;
    raplcap_destroy(rc);
    errno = err_save;
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_init: Initialized\n");
  return 0;
}

int raplcap_destroy(raplcap* rc) {
  uint32_t i;
  int err_save = 0;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != NULL) {
    for (i = 0; i < rc->nsockets; i++) {
      raplcap_log(DEBUG, "raplcap_destroy: socket=%"PRIu32"\n", i);
      if (powercap_rapl_destroy(&((powercap_rapl_pkg*) rc->state)[i])) {
        raplcap_perror(ERROR, "raplcap_destroy: powercap_rapl_destroy");
        err_save = errno;
      }
    }
    free(rc->state);
    rc->state = NULL;
  }
  rc->nsockets = 0;
  raplcap_log(DEBUG, "raplcap_destroy: Destroyed\n");
  errno = err_save;
  return err_save ? -1 : 0;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  return rc->nsockets == 0 ? get_powercap_sockets() : rc->nsockets;
}

int raplcap_is_zone_supported(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  powercap_rapl_zone z;
  const powercap_rapl_pkg* pkg = get_pkg_zone(rc, socket, zone, &z);
  int ret;
  if (pkg == NULL) {
    ret = -1;
  } else if ((ret = powercap_rapl_is_zone_supported(pkg, z)) < 0) {
    raplcap_perror(ERROR, "raplcap_is_zone_supported: powercap_rapl_is_zone_supported");
  }
  raplcap_log(DEBUG, "raplcap_is_zone_supported: socket=%"PRIu32", zone=%d, supported=%d\n", socket, zone, ret);
  return ret;
}

int raplcap_is_zone_enabled(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  powercap_rapl_zone z;
  const powercap_rapl_pkg* pkg = get_pkg_zone(rc, socket, zone, &z);
  int ret;
  if (pkg == NULL) {
    ret = -1;
  } else if ((ret = powercap_rapl_is_enabled(pkg, z)) < 0) {
    raplcap_perror(ERROR, "raplcap_is_zone_enabled: powercap_rapl_is_enabled");
  }
  raplcap_log(DEBUG, "raplcap_is_zone_enabled: socket=%"PRIu32", zone=%d, enabled=%d\n", socket, zone, ret);
  return ret;
}

int raplcap_set_zone_enabled(const raplcap* rc, uint32_t socket, raplcap_zone zone, int enabled) {
  powercap_rapl_zone z;
  const powercap_rapl_pkg* pkg = get_pkg_zone(rc, socket, zone, &z);
  int ret;
  if (pkg == NULL) {
    ret = -1;
  } else {
    raplcap_log(DEBUG, "raplcap_set_zone_enabled: socket=%"PRIu32", zone=%d, enabled=%d\n", socket, zone, enabled);
    if ((ret = powercap_rapl_set_enabled(pkg, z, enabled)) != 0) {
      raplcap_perror(ERROR, "raplcap_set_zone_enabled: powercap_rapl_set_enabled");
    }
  }
  return ret;
}

static int get_constraint(const powercap_rapl_pkg* pkg, powercap_rapl_zone z,
                          powercap_rapl_constraint constraint, raplcap_limit* limit) {
  assert(pkg != NULL);
  assert(limit != NULL);
  static const double ONE_MILLION = 1000000.0;
  uint64_t us, uw;
  if (powercap_rapl_get_time_window_us(pkg, z, constraint, &us)) {
    raplcap_perror(ERROR, "get_constraint: powercap_rapl_get_time_window_us");
    return -1;
  }
  if (powercap_rapl_get_power_limit_uw(pkg, z, constraint, &uw)) {
    raplcap_perror(ERROR, "get_constraint: powercap_rapl_get_power_limit_uw");
    return -1;
  }
  limit->seconds = ((double) us) / ONE_MILLION;
  limit->watts = ((double) uw) / ONE_MILLION;
  raplcap_log(DEBUG, "get_constraint: zone=%d, constraint=%d:\n"
              "\ttime=%.12f s (%"PRIu64" us)\n\tpower=%.12f W (%"PRIu64" uW)\n",
              z, constraint, limit->seconds, us, limit->watts, uw);
  return 0;
}

int raplcap_get_limits(const raplcap* rc, uint32_t socket, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  powercap_rapl_zone z;
  const powercap_rapl_pkg* pkg = get_pkg_zone(rc, socket, zone, &z);
  if (pkg == NULL) {
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_get_limits: socket=%"PRIu32", zone=%d\n", socket, zone);
  if ((limit_long != NULL && get_constraint(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, limit_long)) ||
      (limit_short != NULL && HAS_SHORT_TERM(zone) &&
       get_constraint(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT, limit_short))) {
    return -1;
  }
  return 0;
}

static int set_constraint(const powercap_rapl_pkg* pkg, powercap_rapl_zone z,
                          powercap_rapl_constraint constraint, const raplcap_limit* limit) {
  assert(pkg != NULL);
  assert(limit != NULL);
  static const uint64_t ONE_MILLION = 1000000;
  uint64_t us = ONE_MILLION * limit->seconds;
  uint64_t uw = ONE_MILLION * limit->watts;
  raplcap_log(DEBUG, "set_constraint: zone=%d, constraint=%d:\n"
              "\ttime=%.12f s (%"PRIu64" us)\n\tpower=%.12f W (%"PRIu64" uW)\n",
              z, constraint, limit->seconds, us, limit->watts, uw);
  if (us != 0 && powercap_rapl_set_time_window_us(pkg, z, constraint, us)) {
    raplcap_perror(ERROR, "set_constraint: powercap_rapl_set_time_window_us");
    return -1;
  }
  if (uw != 0 && powercap_rapl_set_power_limit_uw(pkg, z, constraint, uw)) {
    raplcap_perror(ERROR, "set_constraint: powercap_rapl_set_power_limit_uw");
    return -1;
  }
  return 0;
}

int raplcap_set_limits(const raplcap* rc, uint32_t socket, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  powercap_rapl_zone z;
  const powercap_rapl_pkg* pkg = get_pkg_zone(rc, socket, zone, &z);
  if (pkg == NULL) {
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_set_limits: socket=%"PRIu32", zone=%d\n", socket, zone);
  if ((limit_long != NULL && set_constraint(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, limit_long)) ||
      (limit_short != NULL && HAS_SHORT_TERM(zone) &&
       set_constraint(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT, limit_short))) {
    return -1;
  }
  return 0;
}
