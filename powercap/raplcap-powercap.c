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
// powercap headers
#include <powercap.h>
#include <powercap-sysfs.h>

#define RAPLCAP_IMPL "raplcap-powercap"
#include "raplcap.h"
#include "raplcap-wrappers.h"
#include "raplcap-common.h"
#include "powercap-intel-rapl.h"

#define CONTROL_TYPE "intel-rapl"
#define ZONE_NAME_MAX_SIZE 64
#define ZONE_NAME_PREFIX_PACKAGE "package-"

typedef struct raplcap_powercap {
  powercap_intel_rapl_parent* parent_zones;
  uint32_t n_parent_zones;
  uint32_t n_pkg;
  // currently only support homogeneous die count per package
  uint32_t n_die;
} raplcap_powercap;

static raplcap rc_default;

static powercap_intel_rapl_parent* get_parent_zone(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  raplcap_powercap* state;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_powercap*) rc->state) == NULL) {
    // unfortunately can't detect if the context just contains garbage
    raplcap_log(ERROR, "Context is not initialized\n");
    errno = EINVAL;
    return NULL;
  }
  if (pkg >= state->n_pkg) {
    raplcap_log(ERROR, "Package %"PRIu32" not in range [0, %"PRIu32")\n", pkg, state->n_pkg);
    errno = EINVAL;
    return NULL;
  }
  if (die >= state->n_die) {
    raplcap_log(ERROR, "Die %"PRIu32" not in range [0, %"PRIu32")\n", die, state->n_die);
    errno = EINVAL;
    return NULL;
  }
  if ((int) zone < 0 || (int) zone >= RAPLCAP_NZONES) {
    errno = EINVAL;
    return NULL;
  }
  // PSYS is a special case - always stored at the end of the parent_zones array, with index > n_pkg
  // If PSYS is requested and supported, it doesn't matter what pkg or die the caller actually specified
  if (zone == RAPLCAP_ZONE_PSYS &&
      powercap_intel_rapl_is_zone_supported(&state->parent_zones[state->n_parent_zones - 1], zone)) {
    return &state->parent_zones[state->n_parent_zones - 1];
  }
  assert(((pkg * state->n_die) + die) < state->n_parent_zones);
  return &state->parent_zones[(pkg * state->n_die) + die];
}

static uint32_t count_parent_zones(void) {
  uint32_t n = powercap_intel_rapl_get_num_instances();
  raplcap_log(DEBUG, "count_parent_zones: n=%"PRIu32"\n", n);
  return n;
}

static int count_pkg_die(uint32_t* pkg_die_mask, uint32_t n_parent_zones, uint32_t* max_pkg_id) {
  char name[ZONE_NAME_MAX_SIZE];
  char* endptr;
  char* endptr2;
  uint32_t i;
  uint32_t pkg;
  uint32_t die;
  // package and die IDs can appear in any order
  for (i = 0; i < n_parent_zones; i++) {
    if (powercap_sysfs_zone_get_name(CONTROL_TYPE, &i, 1, name, sizeof(name)) < 0) {
      raplcap_perror(ERROR, "count_pkg_die: powercap_sysfs_zone_get_name");
      return -1;
    }
    if (strncmp(name, ZONE_NAME_PREFIX_PACKAGE, sizeof(ZONE_NAME_PREFIX_PACKAGE) - 1)) {
      // not a PACKAGE zone (e.g., could be PSYS)
      continue;
    }
    pkg = strtoul(&name[sizeof(ZONE_NAME_PREFIX_PACKAGE) / sizeof(ZONE_NAME_PREFIX_PACKAGE[0]) - 1], &endptr, 0);
    if (!pkg && endptr == name) {
      // failed to get pkg ID - something unexpected in name format
      raplcap_log(ERROR, "count_pkg_die: Failed to parse package from zone name: %s\n", name);
      errno = EINVAL;
      return -1;
    }
    if (pkg >= n_parent_zones) {
      // something is weird with the kernel zone naming if this happens - packages w/ smaller IDs are missing
      // NOTE: The presence of a PSYS zone should reduce the upper bound, even though the array index is still legal
      //       Package IDs are verified later
      raplcap_log(ERROR, "count_pkg_die: Package %"PRIu32" not in range [0, %"PRIu32")\n", pkg, n_parent_zones);
      errno = EINVAL;
      return -1;
    }
    if (*max_pkg_id < pkg) {
      *max_pkg_id = pkg;
    }
    if (*endptr == '\0') {
      // the string format is package-X
      die = 0;
    } else if (endptr[1] == '-') {
      // the string format is (presumably) package-X-die-Y
      die = strtoul(endptr + 1, &endptr2, 0);
      if (!die && (endptr + 1) == endptr2) {
        //. failed to get die - something unexpected in name format
        raplcap_log(ERROR, "count_pkg_die: Failed to parse die from zone name: %s\n", name);
        errno = EINVAL;
        return -1;
      }
    } else {
      raplcap_log(ERROR, "count_pkg_die: Unsupported zone name format: %s\n", name);
      errno = EINVAL;
      return -1;
    }
    // supports detecting duplicate die entries up to die ID 32
    if (pkg_die_mask[pkg] & (1 << die)) {
      raplcap_log(ERROR, "count_pkg_die: Duplicate package/die detected, pkg=%"PRIu32", die=%"PRIu32"\n", pkg, die);
      errno = EINVAL;
      return -1;
    }
    pkg_die_mask[pkg] |= (1 << die);
  }
  return 0;
}

static int parse_pkg_die(const uint32_t* pkg_die_mask, uint32_t n_parent_zones, uint32_t max_pkg_id, uint32_t* n_pkgs, uint32_t* n_die) {
  uint32_t i;
  uint32_t n_pkg_die;
  for (i = 0; i < n_parent_zones; i++) {
    if (!pkg_die_mask[i]) {
      // not a PACKAGE zone (e.g., could be PSYS)
      continue;
    }
    (*n_pkgs)++;
    // count die
    n_pkg_die = __builtin_popcount(pkg_die_mask[i]);
    // only the lowest bits should be set, otherwise some die are missing
    if ((pkg_die_mask[i] >> n_pkg_die) != 0) {
      raplcap_log(ERROR, "parse_pkg_die: Unexpected package/die mapping - possibly missing package die\n");
      errno = EINVAL;
      return -1;
    }
    if (*n_die == 0) {
      *n_die = n_pkg_die;
    } else if (n_pkg_die != *n_die) {
      // currently only support uniform package die counts, so verify that every package has matching die count
      raplcap_log(ERROR, "parse_pkg_die: Unsupported heterogeneity in package/die configuration\n");
      errno = EINVAL;
      return -1;
    }
  }
  // verify that packages aren't missing from range [0, n_pkgs)
  if (max_pkg_id != *n_pkgs - 1) {
    raplcap_log(ERROR, "parse_pkg_die: Unexpected package/die mapping - possibly missing package\n");
    errno = EINVAL;
    return -1;
  }
  return 0;
}

static int get_topology(uint32_t n_parent_zones, uint32_t* n_pkgs, uint32_t* n_die) {
  uint32_t* pkg_die_mask;
  uint32_t max_pkg_id = 0;
  int ret = 0;
  if (!n_parent_zones) {
    errno = EINVAL;
    return -1;
  }
  if (!(pkg_die_mask = calloc(n_parent_zones, sizeof(uint32_t)))) {
    return -1;
  }
  *n_pkgs = 0;
  *n_die = 0;
  // TODO: Enforce that pkg IDs fill the range [0, n_pkgs), and die IDs fill the range [0, n_die) for each package
  //       This is required to correctly access zones for "pkg" and "die" params used to compute array indexes
  if (!(ret = count_pkg_die(pkg_die_mask, n_parent_zones, &max_pkg_id))) {
    ret = parse_pkg_die(pkg_die_mask, n_parent_zones, max_pkg_id, n_pkgs, n_die);
  }
  raplcap_log(DEBUG, "get_topology: packages=%"PRIu32", die=%"PRIu32"\n", *n_pkgs, *n_die);
  free(pkg_die_mask);
  return ret;
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
  const powercap_intel_rapl_parent* zone_a = (const powercap_intel_rapl_parent*) a;
  const powercap_intel_rapl_parent* zone_b = (const powercap_intel_rapl_parent*) b;
  int ret = 0;
  // first check if either zone is PSYS, which must be placed at the end of the sorted array (after PACKAGE zones)
  // in the current powercap design, PSYS zones exist as their own parent zones, without child zones
  if (powercap_intel_rapl_is_zone_supported(zone_a, RAPLCAP_ZONE_PSYS)) {
    return 1; // a >= b
  }
  if (powercap_intel_rapl_is_zone_supported(zone_b, RAPLCAP_ZONE_PSYS)) {
    return -1; // a < b
  }
  // now assume PACKAGE zones and sort by name
  if (powercap_intel_rapl_get_name(zone_a, RAPLCAP_ZONE_PACKAGE, name_a, sizeof(name_a)) >= 0 &&
      powercap_intel_rapl_get_name(zone_b, RAPLCAP_ZONE_PACKAGE, name_b, sizeof(name_b)) >= 0) {
    // assumes names are in the form "package-X" or "package-X-die-Y"
    if ((ret = strcmp_nat_lu(name_a, name_b)) > 0) {
      raplcap_log(DEBUG, "sort_parent_zones: Zones are out of order\n");
    }
  } else {
    raplcap_perror(ERROR, "sort_parent_zones: powercap_intel_rapl_get_name");
  }
  return ret;
}

int raplcap_init(raplcap* rc) {
  raplcap_powercap* state;
  uint32_t n_parent_zones;
  uint32_t n_pkg;
  uint32_t n_die;
  uint32_t i;
  int err_save;
  const char* env_ro = getenv(ENV_RAPLCAP_READ_ONLY);
  int ro = env_ro == NULL ? 0 : atoi(env_ro);
  if (rc == NULL) {
    rc = &rc_default;
  }
  // get the number of packages
  if ((n_parent_zones = count_parent_zones()) == 0) {
    raplcap_perror(ERROR, "raplcap_init: No RAPL zones found");
    return -1;
  }
  if (get_topology(n_parent_zones, &n_pkg, &n_die) < 0) {
    return -1;
  }
  if ((state = malloc(sizeof(raplcap_powercap))) == NULL) {
    return -1;
  }
  if ((state->parent_zones = malloc(n_parent_zones * sizeof(powercap_intel_rapl_parent))) == NULL) {
    free(state);
    return -1;
  }
  state->n_parent_zones = n_parent_zones;
  state->n_pkg = n_pkg;
  state->n_die = n_die;
  rc->state = state;
  for (i = 0; i < state->n_parent_zones; i++) {
    if (powercap_intel_rapl_init(i, &state->parent_zones[i], ro)) {
      raplcap_perror(ERROR, "raplcap_init: powercap_intel_rapl_init");
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
  qsort(state->parent_zones, state->n_parent_zones, sizeof(powercap_intel_rapl_parent), sort_parent_zones);
  if (errno) {
    raplcap_log(ERROR, "raplcap_init: Failed to sort packages by name\n");
    err_save = errno;
    raplcap_destroy(rc);
    errno = err_save;
    return -1;
  }
  rc->nsockets = n_pkg;
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
  if ((state = (raplcap_powercap*) rc->state) != NULL) {
    for (i = 0; i < state->n_parent_zones; i++) {
      raplcap_log(DEBUG, "raplcap_destroy: zone=%"PRIu32"\n", i);
      if (powercap_intel_rapl_destroy(&state->parent_zones[i])) {
        raplcap_perror(ERROR, "raplcap_destroy: powercap_intel_rapl_destroy");
        err_save = errno;
      }
    }
    free(state);
    rc->state = NULL;
  }
  rc->nsockets = 0;
  raplcap_log(DEBUG, "raplcap_destroy: Destroyed\n");
  errno = err_save;
  return err_save ? -1 : 0;
}

static int get_topology_uninit(uint32_t* n_pkg, uint32_t* n_die) {
  uint32_t n_parent_zones;
  if ((n_parent_zones = count_parent_zones()) == 0) {
    return -1;
  }
  return get_topology(n_parent_zones, n_pkg, n_die);
}

uint32_t raplcap_get_num_packages(const raplcap* rc) {
  const raplcap_powercap* state;
  uint32_t n_pkg = 0;
  uint32_t n_die;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_powercap*) rc->state) != NULL) {
    return state->n_pkg;
  }
  return get_topology_uninit(&n_pkg, &n_die) ? 0 : n_pkg;
}

uint32_t raplcap_get_num_die(const raplcap* rc, uint32_t pkg) {
  const raplcap_powercap* state;
  uint32_t n_pkg;
  uint32_t n_die;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_powercap*) rc->state) != NULL) {
    if (pkg >= state->n_pkg) {
      raplcap_log(ERROR, "raplcap_get_num_die: Package %"PRIu32" not in range [0, %"PRIu32")\n", pkg, state->n_pkg);
      errno = EINVAL;
      return 0;
    }
    return state->n_die;
  }
  if (get_topology_uninit(&n_pkg, &n_die)) {
    return 0;
  }
  if (pkg >= n_pkg) {
    raplcap_log(ERROR, "raplcap_get_num_die: Package %"PRIu32" not in range [0, %"PRIu32")\n", pkg, n_pkg);
    errno = EINVAL;
    return 0;
  }
  return n_die;
}

int raplcap_pd_is_zone_supported(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  int ret;
  if (p == NULL) {
    return -1;
  }
  ret = powercap_intel_rapl_is_zone_supported(p, zone);
  raplcap_log(DEBUG, "raplcap_pd_is_zone_supported: pkg=%"PRIu32", die=%"PRIu32", zone=%d, supported=%d\n",
              pkg, die, zone, ret);
  return ret;
}

int raplcap_pd_is_constraint_supported(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                                       raplcap_constraint constraint) {
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  int ret;
  if (p == NULL) {
    return -1;
  }
  if ((int) constraint < 0 || (int) constraint >= RAPLCAP_NCONSTRAINTS) {
    errno = EINVAL;
    return -1;
  }
  ret = powercap_intel_rapl_is_constraint_supported(p, zone, constraint);
  raplcap_log(DEBUG,
              "raplcap_pd_is_constraint_supported: pkg=%"PRIu32", die=%"PRIu32", zone=%d, constraint=%d, supported=%d\n",
              pkg, die, zone, constraint, ret);
  return ret;
}

int raplcap_pd_is_zone_enabled(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  int ret;
  if (p == NULL) {
    ret = -1;
  } else if ((ret = powercap_intel_rapl_is_enabled(p, zone)) < 0) {
    raplcap_perror(ERROR, "raplcap_pd_is_zone_enabled: powercap_intel_rapl_is_enabled");
  }
  raplcap_log(DEBUG, "raplcap_pd_is_zone_enabled: pkg=%"PRIu32", die=%"PRIu32", zone=%d, enabled=%d\n",
              pkg, die, zone, ret);
  return ret;
}

int raplcap_pd_set_zone_enabled(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone, int enabled) {
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  int ret;
  if (p == NULL) {
    ret = -1;
  } else {
    raplcap_log(DEBUG, "raplcap_pd_set_zone_enabled: pkg=%"PRIu32", die=%"PRIu32", zone=%d, enabled=%d\n",
                pkg, die, zone, enabled);
    if ((ret = powercap_intel_rapl_set_enabled(p, zone, enabled)) != 0) {
      raplcap_perror(ERROR, "raplcap_pd_set_zone_enabled: powercap_intel_rapl_set_enabled");
    }
  }
  return ret;
}

static int get_constraint(const powercap_intel_rapl_parent* p, raplcap_zone z,
                          raplcap_constraint constraint, raplcap_limit* limit) {
  assert(p != NULL);
  assert(limit != NULL);
  static const double ONE_MILLION = 1000000.0;
  uint64_t us, uw;
  if (powercap_intel_rapl_get_time_window_us(p, z, constraint, &us)) {
    raplcap_perror(ERROR, "get_constraint: powercap_intel_rapl_get_time_window_us");
    return -1;
  }
  if (powercap_intel_rapl_get_power_limit_uw(p, z, constraint, &uw)) {
    raplcap_perror(ERROR, "get_constraint: powercap_intel_rapl_get_power_limit_uw");
    return -1;
  }
  limit->seconds = ((double) us) / ONE_MILLION;
  limit->watts = ((double) uw) / ONE_MILLION;
  raplcap_log(DEBUG, "get_constraint: zone=%d, constraint=%d:\n"
              "\ttime=%.12f s (%"PRIu64" us)\n\tpower=%.12f W (%"PRIu64" uW)\n",
              z, constraint, limit->seconds, us, limit->watts, uw);
  return 0;
}

int raplcap_pd_get_limits(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                          raplcap_limit* limit_long, raplcap_limit* limit_short) {
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  if (p == NULL) {
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_get_limits: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if ((limit_long != NULL && get_constraint(p, zone, RAPLCAP_CONSTRAINT_LONG_TERM, limit_long)) ||
      (limit_short != NULL &&
       powercap_intel_rapl_is_constraint_supported(p, zone, RAPLCAP_CONSTRAINT_SHORT_TERM) > 0 &&
       get_constraint(p, zone, RAPLCAP_CONSTRAINT_SHORT_TERM, limit_short))) {
    return -1;
  }
  return 0;
}

static int set_constraint(const powercap_intel_rapl_parent* p, raplcap_zone z,
                          raplcap_constraint constraint, const raplcap_limit* limit) {
  assert(p != NULL);
  assert(limit != NULL);
  static const uint64_t ONE_MILLION = 1000000;
  uint64_t us = ONE_MILLION * limit->seconds;
  uint64_t uw = ONE_MILLION * limit->watts;
  raplcap_log(DEBUG, "set_constraint: zone=%d, constraint=%d:\n"
              "\ttime=%.12f s (%"PRIu64" us)\n\tpower=%.12f W (%"PRIu64" uW)\n",
              z, constraint, limit->seconds, us, limit->watts, uw);
  if (us != 0 && powercap_intel_rapl_set_time_window_us(p, z, constraint, us)) {
    raplcap_perror(ERROR, "set_constraint: powercap_intel_rapl_set_time_window_us");
    return -1;
  }
  if (uw != 0 && powercap_intel_rapl_set_power_limit_uw(p, z, constraint, uw)) {
    raplcap_perror(ERROR, "set_constraint: powercap_intel_rapl_set_power_limit_uw");
    return -1;
  }
  return 0;
}

int raplcap_pd_set_limits(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                          const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  if (p == NULL) {
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_set_limits: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if ((limit_long != NULL && set_constraint(p, zone, RAPLCAP_CONSTRAINT_LONG_TERM, limit_long)) ||
      (limit_short != NULL &&
       powercap_intel_rapl_is_constraint_supported(p, zone, RAPLCAP_CONSTRAINT_SHORT_TERM) > 0 &&
       set_constraint(p, zone, RAPLCAP_CONSTRAINT_SHORT_TERM, limit_short))) {
    return -1;
  }
  return 0;
}

int raplcap_pd_get_limit(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                         raplcap_constraint constraint, raplcap_limit* limit) {
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  if (p == NULL) {
    return -1;
  }
  if ((int) constraint < 0 || (int) constraint >= RAPLCAP_NCONSTRAINTS) {
    errno = EINVAL;
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_get_limit: pkg=%"PRIu32", die=%"PRIu32", zone=%d, constraint=%d\n",
              pkg, die, zone, constraint);
  if (limit != NULL && get_constraint(p, zone, constraint, limit)) {
    return -1;
  }
  return 0;
}

int raplcap_pd_set_limit(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                         raplcap_constraint constraint, const raplcap_limit* limit) {
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  if (p == NULL) {
    return -1;
  }
  if ((int) constraint < 0 || (int) constraint >= RAPLCAP_NCONSTRAINTS) {
    errno = EINVAL;
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_set_limit: pkg=%"PRIu32", die=%"PRIu32", zone=%d, constraint=%d\n",
              pkg, die, zone, constraint);
  if (limit != NULL && set_constraint(p, zone, constraint, limit)) {
    return -1;
  }
  return 0;
}

double raplcap_pd_get_energy_counter(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  uint64_t uj;
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  if (p == NULL) {
    return -1;
  }
  if (powercap_intel_rapl_get_energy_uj(p, zone, &uj)) {
    raplcap_perror(ERROR, "raplcap_pd_get_energy_counter: powercap_intel_rapl_get_energy_uj");
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_get_energy_counter: pkg=%"PRIu32", die=%"PRIu32", zone=%d, uj=%"PRIu64"\n",
              pkg, die, zone, uj);
  return uj / 1000000.0;
}

double raplcap_pd_get_energy_counter_max(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  uint64_t uj;
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  if (p == NULL) {
    return -1;
  }
  if (powercap_intel_rapl_get_max_energy_range_uj(p, zone, &uj)) {
    raplcap_perror(ERROR, "raplcap_pd_get_energy_counter_max: powercap_intel_rapl_get_max_energy_range_uj");
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_get_energy_counter_max: pkg=%"PRIu32", die=%"PRIu32", zone=%d, uj=%"PRIu64"\n",
              pkg, die, zone, uj);
  return uj / 1000000.0;
}
