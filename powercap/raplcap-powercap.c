/**
 * Implementation that wraps libpowercap with RAPL sysfs interface.
 *
 * @author Connor Imes
 * @date 2016-05-13
 */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "raplcap.h"
#define RAPLCAP_IMPL "raplcap-powercap"
#include "raplcap-common.h"
// powercap header
#include <powercap-rapl.h>
#include <powercap-sysfs.h>

#define CONTROL_TYPE "intel-rapl"
#define ZONE_NAME_MAX_SIZE 64
#define ZONE_NAME_PREFIX_PACKAGE "package"

#define HAS_SHORT_TERM(pkg, z) (powercap_rapl_is_constraint_supported(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT) > 0)

typedef struct raplcap_powercap {
  powercap_rapl_pkg* parent_zones;
  uint32_t n_parent_zones;
} raplcap_powercap;

static raplcap rc_default;

static powercap_rapl_pkg* get_parent_zone(const raplcap* rc, uint32_t socket, raplcap_zone zone, powercap_rapl_zone* z) {
  assert(z != NULL);
  raplcap_powercap* state;
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
    raplcap_log(ERROR, "get_parent_zone: Context is not initialized\n");
    errno = EINVAL;
    return NULL;
  }
  if (socket >= rc->nsockets) {
    raplcap_log(ERROR, "get_parent_zone: Socket %"PRIu32" not in range [0, %"PRIu32")\n", socket, rc->nsockets);
    errno = EINVAL;
    return NULL;
  }
  if ((int) zone < 0 || (int) zone > RAPLCAP_ZONE_PSYS) {
    raplcap_log(ERROR, "get_parent_zone: Unknown zone: %d\n", zone);
    errno = EINVAL;
    return NULL;
  }
  *z = POWERCAP_RAPL_ZONES[zone];
  state = (raplcap_powercap*) rc->state;
  // PSYS is a special case - always stored at the end of the parent_zones array, with index > rc->nsockets
  // If PSYS is requested and supported, it doesn't matter what socket the caller actually specified
  if (*z == POWERCAP_RAPL_ZONE_PSYS &&
      powercap_rapl_is_zone_supported(&state->parent_zones[state->n_parent_zones - 1], *z)) {
    return &state->parent_zones[state->n_parent_zones - 1];
  }
  return &state->parent_zones[socket];
}

static uint32_t count_parent_zones(void) {
  uint32_t n = powercap_rapl_get_num_instances();
  if (n == 0) {
    raplcap_perror(ERROR, "count_parent_zones: powercap_rapl_get_num_instances");
  }
  raplcap_log(DEBUG, "count_parent_zones: n=%"PRIu32"\n", n);
  return n;
}

static uint32_t count_package_zones(uint32_t n_parent_zones) {
  char name[ZONE_NAME_MAX_SIZE];
  uint32_t i;
  uint32_t n = 0;
  for (i = 0; i < n_parent_zones; i++) {
    name[0] = '\0';
    if (powercap_sysfs_zone_get_name(CONTROL_TYPE, &i, 1, name, sizeof(name)) < 0) {
      raplcap_perror(ERROR, "count_package_zones: powercap_sysfs_zone_get_name");
      n = 0;
      break;
    }
    if (!strncmp(name, ZONE_NAME_PREFIX_PACKAGE, sizeof(ZONE_NAME_PREFIX_PACKAGE) - 1)) {
      n++;
    } // else not a PACKAGE zone (e.g., could be PSYS)
  }
  raplcap_log(DEBUG, "count_package_zones: n=%"PRIu32"\n", n);
  return n;
}

// compare strings that may contain substrings of natural numbers (values >= 0)
// format is expected to be: "package-%d" or "package-%d-die-%d"
static int strcmp_nat_lu(const char* l, const char* r) {
  unsigned long l_val;
  unsigned long r_val;
  long diff;
  char* endptr;
  int is_num_block = 0;
  while (*l && *r) {
    if (is_num_block) {
      // parse numeric values
      l_val = strtoul(l, &endptr, 0);
      l = endptr;
      r_val = strtoul(r, &endptr, 0);
      r = endptr;
      diff = l_val - r_val;
      if (diff > 0) {
        return 1;
      }
      if (diff < 0) {
        return -1;
      }
      is_num_block = 0;
    } else {
      // compare non-numerically until both strings are digits at same index
      for (; *l && *r; l++, r++) {
        if (isdigit(*l) && isdigit(*r)) {
          is_num_block = 1;
          break;
        }
        if (isdigit(*l)) {
          return -1;
        }
        if (isdigit(*r)) {
          return 1;
        }
        diff = *l - *r;
        if (diff > 0) {
          return 1;
        }
        if (diff < 0) {
          return -1;
        }
      }
    }
  }
  return *r ? -1 : (*l ? 1 : 0);
}

static int sort_parent_zones(const void* a, const void* b) {
  char name_a[ZONE_NAME_MAX_SIZE] = { 0 };
  char name_b[ZONE_NAME_MAX_SIZE] = { 0 };
  const powercap_rapl_pkg* zone_a = (const powercap_rapl_pkg*) a;
  const powercap_rapl_pkg* zone_b = (const powercap_rapl_pkg*) b;
  int ret;
  // first check if either zone is PSYS, which must be placed at the end of the sorted array (after PACKAGE zones)
  // in the current powercap design, PSYS zones exist as their own parent zones, without child zones
  ret = powercap_rapl_is_zone_supported(zone_a, POWERCAP_RAPL_ZONE_PSYS);
  if (ret < 0) {
    raplcap_perror(ERROR, "sort_parent_zones: powercap_rapl_is_zone_supported");
    return 0;
  }
  if (ret > 0) {
    return 1; // a >= b
  }
  ret = powercap_rapl_is_zone_supported(zone_b, POWERCAP_RAPL_ZONE_PSYS);
  if (ret < 0) {
    raplcap_perror(ERROR, "sort_parent_zones: powercap_rapl_is_zone_supported");
    return 0;
  }
  if (ret > 0) {
    return -1; // a < b
  }
  // now assume PACKAGE zones and sort by name
  if (powercap_rapl_get_name(zone_a, POWERCAP_RAPL_ZONE_PACKAGE, name_a, sizeof(name_a)) >= 0 &&
      powercap_rapl_get_name(zone_b, POWERCAP_RAPL_ZONE_PACKAGE, name_b, sizeof(name_b)) >= 0) {
    // assumes names are in the form "package-X" or "package-X-die-Y"
    if ((ret = strcmp_nat_lu(name_a, name_b)) > 0) {
      raplcap_log(DEBUG, "sort_parent_zones: Zones are out of order\n");
    }
  } else {
    raplcap_perror(ERROR, "sort_parent_zones: powercap_rapl_get_name");
    ret = 0;
  }
  return ret;
}

int raplcap_init(raplcap* rc) {
  raplcap_powercap* state;
  uint32_t n_parent_zones;
  uint32_t i;
  int err_save;
  const char* env_ro = getenv(ENV_RAPLCAP_READ_ONLY);
  int ro = env_ro == NULL ? 0 : atoi(env_ro);
  if (rc == NULL) {
    rc = &rc_default;
  }
  // get the number of packages/sockets
  if ((n_parent_zones = count_parent_zones()) == 0) {
    return -1;
  }
  if ((rc->nsockets = count_package_zones(n_parent_zones)) == 0) {
    return -1;
  }
  if ((state = malloc(sizeof(raplcap_powercap))) == NULL) {
    raplcap_perror(ERROR, "raplcap_init: malloc");
    rc->nsockets = 0;
    return -1;
  }
  if ((state->parent_zones = malloc(n_parent_zones * sizeof(powercap_rapl_pkg))) == NULL) {
    raplcap_perror(ERROR, "raplcap_init: malloc");
    rc->nsockets = 0;
    free(state);
    return -1;
  }
  state->n_parent_zones = n_parent_zones;
  rc->state = state;
  for (i = 0; i < state->n_parent_zones; i++) {
    if (powercap_rapl_init(i, &state->parent_zones[i], ro)) {
      raplcap_perror(ERROR, "raplcap_init: powercap_rapl_init");
      err_save = errno;
      state->n_parent_zones = i; // so as not to cleanup uninitialized zones
      raplcap_destroy(rc);
      errno = err_save;
      return -1;
    }
  }
  // it's been observed that packages in sysfs may be numbered out of order; we must sort by name
  // if there are PSYS zones, we sort them to the end of the array
  errno = 0;
  qsort(state->parent_zones, state->n_parent_zones, sizeof(powercap_rapl_pkg), sort_parent_zones);
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
  raplcap_powercap* state;
  uint32_t i;
  int err_save = 0;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != NULL) {
    state = (raplcap_powercap*) rc->state;
    for (i = 0; i < state->n_parent_zones; i++) {
      raplcap_log(DEBUG, "raplcap_destroy: zone=%"PRIu32"\n", i);
      if (powercap_rapl_destroy(&state->parent_zones[i])) {
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
  return rc->nsockets == 0 ? count_package_zones(count_parent_zones()) : rc->nsockets;
}

int raplcap_is_zone_supported(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  powercap_rapl_zone z;
  const powercap_rapl_pkg* pkg = get_parent_zone(rc, socket, zone, &z);
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
  const powercap_rapl_pkg* pkg = get_parent_zone(rc, socket, zone, &z);
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
  const powercap_rapl_pkg* pkg = get_parent_zone(rc, socket, zone, &z);
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
  const powercap_rapl_pkg* pkg = get_parent_zone(rc, socket, zone, &z);
  if (pkg == NULL) {
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_get_limits: socket=%"PRIu32", zone=%d\n", socket, zone);
  if ((limit_long != NULL && get_constraint(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, limit_long)) ||
      (limit_short != NULL && HAS_SHORT_TERM(pkg, z) &&
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
  const powercap_rapl_pkg* pkg = get_parent_zone(rc, socket, zone, &z);
  if (pkg == NULL) {
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_set_limits: socket=%"PRIu32", zone=%d\n", socket, zone);
  if ((limit_long != NULL && set_constraint(pkg, z, POWERCAP_RAPL_CONSTRAINT_LONG, limit_long)) ||
      (limit_short != NULL && HAS_SHORT_TERM(pkg, z) &&
       set_constraint(pkg, z, POWERCAP_RAPL_CONSTRAINT_SHORT, limit_short))) {
    return -1;
  }
  return 0;
}

double raplcap_get_energy_counter(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  powercap_rapl_zone z;
  uint64_t uj;
  const powercap_rapl_pkg* pkg = get_parent_zone(rc, socket, zone, &z);
  if (pkg == NULL) {
    return -1;
  }
  if (powercap_rapl_get_energy_uj(pkg, z, &uj)) {
    raplcap_perror(ERROR, "raplcap_get_energy_counter: powercap_rapl_get_energy_uj");
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_get_energy_counter: socket=%"PRIu32", zone=%d, uj=%"PRIu64"\n", socket, zone, uj);
  return uj / 1000000.0;
}

double raplcap_get_energy_counter_max(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  powercap_rapl_zone z;
  uint64_t uj;
  const powercap_rapl_pkg* pkg = get_parent_zone(rc, socket, zone, &z);
  if (pkg == NULL) {
    return -1;
  }
  if (powercap_rapl_get_max_energy_range_uj(pkg, z, &uj)) {
    raplcap_perror(ERROR, "raplcap_get_energy_counter_max: powercap_rapl_get_max_energy_range_uj");
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_get_energy_counter_max: socket=%"PRIu32", zone=%d, uj=%"PRIu64"\n", socket, zone, uj);
  return uj / 1000000.0;
}
