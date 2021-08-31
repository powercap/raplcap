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

#include "raplcap.h"
#include "raplcap-wrappers.h"
#include "raplcap-common.h"
#include "powercap-intel-rapl.h"

#define CONTROL_TYPE "intel-rapl"
#define ZONE_NAME_MAX_SIZE 64
#define ZONE_NAME_PREFIX_PACKAGE "package-"

typedef struct raplcap_powercap_parent {
  powercap_intel_rapl_parent p;
  raplcap_zone type;
  int has_pkg;
  uint32_t pkg;
  int has_die;
  uint32_t die;
} raplcap_powercap_parent;

typedef struct raplcap_powercap {
  raplcap_powercap_parent* parent_zones;
  raplcap_powercap_parent** pkg_zones;
  raplcap_powercap_parent** psys_zones;
  uint32_t n_parent_zones;
  uint32_t n_pkg;
  // currently only support homogeneous die count per package
  uint32_t n_die;
} raplcap_powercap;

static raplcap rc_default;

static powercap_intel_rapl_parent* get_parent_zone(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  raplcap_powercap* state;
  raplcap_powercap_parent* p = NULL;
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
  if (zone == RAPLCAP_ZONE_PSYS) {
    // powercap control type doesn't specify die values for PSYS zones, so we assume die must be 0
    if (die == 0) {
      p = state->psys_zones[pkg];
    }
    // --- begin deprecation block
    if (p == NULL) {
      if ((p = state->psys_zones[pkg]) != NULL) {
        raplcap_log(WARN, "Ignoring die value > 0 for PSYS zone at pkg=%"PRIu32".\nThis behavior is deprecated - "
                    "in the future, an error will be returned if the zone is not found for the specified pkg/die.\n",
                    pkg);
      }
      if (p == NULL && pkg > 0 && (p = state->psys_zones[0]) != NULL) {
        raplcap_log(WARN, "Falling back on PSYS zone at pkg=0, die=0.\nThis behavior is deprecated - "
                    "in the future, an error will be returned if the zone is not found for the specified pkg/die.\n");
      }
    }
    // --- end deprecation block
    // if p is still NULL, fall through and later code will (correctly) fail to find PSYS within the regular parent zone
  }
  if (p == NULL) {
    p = state->pkg_zones[(pkg * state->n_die) + die];
  }
  if (p == NULL) {
    // the requested package/die was in range, but the zone was not detected in sysfs
    errno = ENODEV;
    return NULL;
  }
  return &p->p;
}

static int get_topology(uint32_t *n_parent_zones, uint32_t* n_pkg, uint32_t* n_die) {
  char name[ZONE_NAME_MAX_SIZE];
  char* endptr;
  char* endptr2;
  uint32_t pkg;
  uint32_t die;
  uint32_t max_pkg_id = 0;
  uint32_t max_die_id = 0;
  // package and die IDs can appear in any order
  for (*n_parent_zones = 0; !powercap_sysfs_zone_exists(CONTROL_TYPE, n_parent_zones, 1); (*n_parent_zones)++) {
    if (powercap_sysfs_zone_get_name(CONTROL_TYPE, n_parent_zones, 1, name, sizeof(name)) < 0) {
      raplcap_perror(ERROR, "powercap_sysfs_zone_get_name");
      return -1;
    }
    if (strncmp(name, ZONE_NAME_PREFIX_PACKAGE, sizeof(ZONE_NAME_PREFIX_PACKAGE) - 1)) {
      // not a PACKAGE zone (e.g., could be PSYS)
      continue;
    }
    pkg = strtoul(&name[sizeof(ZONE_NAME_PREFIX_PACKAGE) / sizeof(ZONE_NAME_PREFIX_PACKAGE[0]) - 1], &endptr, 0);
    if (!pkg && endptr == name) {
      // failed to get pkg ID - something unexpected in name format
      raplcap_log(ERROR, "Failed to parse package from zone name: %s\n", name);
      errno = EINVAL;
      return -1;
    }
    if (max_pkg_id < pkg) {
      max_pkg_id = pkg;
    }
    if (*endptr == '\0') {
      // the string format is package-X
      die = 0;
    } else if (endptr[1] == '-') {
      // the string format is (presumably) package-X-die-Y
      die = strtoul(endptr + 1, &endptr2, 0);
      if (!die && (endptr + 1) == endptr2) {
        // failed to get die - something unexpected in name format
        raplcap_log(ERROR, "Failed to parse die from zone name: %s\n", name);
        errno = EINVAL;
        return -1;
      }
      if (max_die_id < die) {
        max_die_id = die;
      }
    } else {
      raplcap_log(ERROR, "Unsupported zone name format: %s\n", name);
      errno = EINVAL;
      return -1;
    }
  }
  if (*n_parent_zones == 0) {
    errno = ENODEV;
    return -1;
  }
  *n_pkg = max_pkg_id + 1;
  *n_die = max_die_id + 1;
  raplcap_log(DEBUG, "get_topology: n_parent_zones=%"PRIu32", n_pkg=%"PRIu32", n_die=%"PRIu32"\n",
              *n_parent_zones, *n_pkg, *n_die);
  return 0;
}

static int parse_parent_zone_topology(raplcap_powercap_parent* rp, uint32_t id) {
  char name[ZONE_NAME_MAX_SIZE] = { 0 };
  const char* ptr = name;
  char* endptr;
  // first determine zone type
  if (powercap_intel_rapl_is_zone_supported(&rp->p, RAPLCAP_ZONE_PACKAGE)) {
    rp->type = RAPLCAP_ZONE_PACKAGE;
  } else if (powercap_intel_rapl_is_zone_supported(&rp->p, RAPLCAP_ZONE_PSYS)) {
    rp->type = RAPLCAP_ZONE_PSYS;
  } else {
    raplcap_log(ERROR, "Unexpected type for parent zone id=%"PRIu32"\n", id);
    errno = ENOTSUP;
    return -1;
  }
  raplcap_log(DEBUG, "parse_parent_zone_topology: id=%"PRIu32", type=%u\n", id, rp->type);
  // now parse name for pkg and die, if available
  // name format is expected to be: "package-%d", "package-%d-die-%d", "psys", or "psys-%d"
  if (powercap_intel_rapl_get_name(&rp->p, rp->type, name, sizeof(name)) < 0) {
    raplcap_perror(ERROR, "powercap_intel_rapl_get_name");
    return -1;
  }
  while (*ptr) {
    if (isdigit(*ptr)) {
      if (!rp->has_pkg) {
        endptr = NULL;
        rp->pkg = strtoul(ptr, &endptr, 0);
        if (!rp->pkg && endptr == ptr) {
          raplcap_log(ERROR, "Failed to parse package from zone name: %s\n", name);
          errno = ENOTSUP;
          return -1;
        }
        rp->has_pkg = 1;
        raplcap_log(DEBUG, "parse_parent_zone_topology: id=%"PRIu32", pkg=%"PRIu32"\n", id, rp->pkg);
      } else if (!rp->has_die) {
        endptr = NULL;
        rp->die = strtoul(ptr, &endptr, 0);
        if (!rp->die && endptr == ptr) {
          raplcap_log(ERROR, "Failed to parse die from zone name: %s\n", name);
          errno = ENOTSUP;
          return -1;
        }
        rp->has_die = 1;
        raplcap_log(DEBUG, "parse_parent_zone_topology: id=%"PRIu32", die=%"PRIu32"\n", id, rp->die);
      } else {
        raplcap_log(ERROR, "Unsupported name format for parent zone id=%"PRIu32": %s\n", id, name);
        errno = ENOTSUP;
        return -1;
      }
      ptr = endptr;
    } else {
      ptr++;
    }
  }
  return 0;
}

// rp is expected to be zero-initialized
static int raplcap_powercap_parent_init(raplcap_powercap_parent* rp, uint32_t id, int ro) {
  int err_save;
  if (powercap_intel_rapl_init(id, &rp->p, ro)) {
    raplcap_perror(ERROR, "powercap_intel_rapl_init");
    return -1;
  }
  if (parse_parent_zone_topology(rp, id)) {
    err_save = errno;
    powercap_intel_rapl_destroy(&rp->p);
    errno = err_save;
    return -1;
  }
  return 0;
}

int raplcap_init(raplcap* rc) {
  raplcap_powercap* state;
  uint32_t n_parent_zones = 0;
  uint32_t n_pkg;
  uint32_t n_die;
  uint32_t i;
  uint32_t pkg;
  uint32_t die;
  int err_save;
  const char* env_ro = getenv(ENV_RAPLCAP_READ_ONLY);
  int ro = env_ro == NULL ? 0 : atoi(env_ro);
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (get_topology(&n_parent_zones, &n_pkg, &n_die) < 0) {
    if (n_parent_zones == 0) {
      raplcap_perror(ERROR, "No RAPL zones found");
    }
    return -1;
  }
  if ((state = malloc(sizeof(raplcap_powercap))) == NULL) {
    return -1;
  }
  if ((state->parent_zones = calloc(n_parent_zones, sizeof(*state->parent_zones))) == NULL) {
    free(state);
    return -1;
  }
  if ((state->pkg_zones = calloc(n_pkg * n_die, sizeof(*state->pkg_zones))) == NULL) {
    free(state->parent_zones);
    free(state);
    return -1;
  }
  if ((state->psys_zones = calloc(n_pkg, sizeof(*state->psys_zones))) == NULL) {
    free(state->pkg_zones);
    free(state->parent_zones);
    free(state);
    return -1;
  }
  state->n_parent_zones = n_parent_zones;
  state->n_pkg = n_pkg;
  state->n_die = n_die;
  rc->state = state;
  for (i = 0; i < state->n_parent_zones; i++) {
    if (raplcap_powercap_parent_init(&state->parent_zones[i], i, ro)) {
      err_save = errno;
      state->n_parent_zones = i; // so as not to cleanup uninitialized zones
      raplcap_destroy(rc);
      errno = err_save;
      return -1;
    }
  }
  // Parent zones in sysfs may be out of order - index by type, package, and die
  for (i = 0; i < state->n_parent_zones; i++) {
    pkg = state->parent_zones[i].pkg;
    die = state->parent_zones[i].die;
    if (pkg >= n_pkg || die >= n_die) {
      // this should only arise if sysfs has changed since we initially parsed topology - unlikely, but possible...
      raplcap_log(ERROR, "Package or die out of range for parent zone id=%"PRIu32"\n", i);
      err_save = errno;
      raplcap_destroy(rc);
      errno = err_save;
      return -1;
    }
    switch (state->parent_zones[i].type) {
      case RAPLCAP_ZONE_PACKAGE:
        if (state->pkg_zones[(pkg * n_die) + die] == NULL) {
          state->pkg_zones[(pkg * n_die) + die] = &state->parent_zones[i];
        } else {
          raplcap_log(WARN, "Ignoring duplicate package entry at parent zone id=%"PRIu32"\n", i);
        }
        break;
      case RAPLCAP_ZONE_PSYS:
        if (state->psys_zones[pkg] == NULL) {
          state->psys_zones[pkg] = &state->parent_zones[i];
        } else {
          raplcap_log(WARN, "Ignoring duplicate psys entry at parent zone id=%"PRIu32"\n", i);
        }
        break;
      default:
        raplcap_log(WARN, "Ignoring unknown type at parent zone id=%"PRIu32"\n", i);
        break;
    }
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
      if (powercap_intel_rapl_destroy(&state->parent_zones[i].p)) {
        raplcap_perror(WARN, "powercap_intel_rapl_destroy");
        err_save = errno;
      }
    }
    free(state->psys_zones);
    free(state->pkg_zones);
    free(state->parent_zones);
    free(state);
    rc->state = NULL;
  }
  rc->nsockets = 0;
  raplcap_log(DEBUG, "raplcap_destroy: Destroyed\n");
  errno = err_save;
  return err_save ? -1 : 0;
}

uint32_t raplcap_get_num_packages(const raplcap* rc) {
  const raplcap_powercap* state;
  uint32_t n_parent_zones;
  uint32_t n_pkg = 0;
  uint32_t n_die;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_powercap*) rc->state) != NULL) {
    return state->n_pkg;
  }
  return get_topology(&n_parent_zones, &n_pkg, &n_die) ? 0 : n_pkg;
}

uint32_t raplcap_get_num_die(const raplcap* rc, uint32_t pkg) {
  const raplcap_powercap* state;
  uint32_t n_parent_zones;
  uint32_t n_pkg;
  uint32_t n_die = 0;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_powercap*) rc->state) != NULL) {
    if (pkg >= state->n_pkg) {
      raplcap_log(ERROR, "Package %"PRIu32" not in range [0, %"PRIu32")\n", pkg, state->n_pkg);
      errno = EINVAL;
      return 0;
    }
    return state->n_die;
  }
  if (get_topology(&n_parent_zones, &n_pkg, &n_die)) {
    return 0;
  }
  if (pkg >= n_pkg) {
    raplcap_log(ERROR, "Package %"PRIu32" not in range [0, %"PRIu32")\n", pkg, n_pkg);
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
    return -1;
  }
  if ((ret = powercap_intel_rapl_is_enabled(p, zone)) < 0) {
    raplcap_perror(ERROR, "powercap_intel_rapl_is_enabled");
  }
  raplcap_log(DEBUG, "raplcap_pd_is_zone_enabled: pkg=%"PRIu32", die=%"PRIu32", zone=%d, enabled=%d\n",
              pkg, die, zone, ret);
  return ret;
}

int raplcap_pd_set_zone_enabled(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone, int enabled) {
  const powercap_intel_rapl_parent* p = get_parent_zone(rc, pkg, die, zone);
  int ret;
  raplcap_log(DEBUG, "raplcap_pd_set_zone_enabled: pkg=%"PRIu32", die=%"PRIu32", zone=%d, enabled=%d\n",
              pkg, die, zone, enabled);
  if (p == NULL) {
    return -1;
  }
  if ((ret = powercap_intel_rapl_set_enabled(p, zone, enabled)) != 0) {
    raplcap_perror(ERROR, "powercap_intel_rapl_set_enabled");
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
    raplcap_perror(ERROR, "powercap_intel_rapl_get_time_window_us");
    return -1;
  }
  if (powercap_intel_rapl_get_power_limit_uw(p, z, constraint, &uw)) {
    raplcap_perror(ERROR, "powercap_intel_rapl_get_power_limit_uw");
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
    raplcap_perror(ERROR, "powercap_intel_rapl_set_time_window_us");
    return -1;
  }
  if (uw != 0 && powercap_intel_rapl_set_power_limit_uw(p, z, constraint, uw)) {
    raplcap_perror(ERROR, "powercap_intel_rapl_set_power_limit_uw");
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
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_get_energy_counter_max: pkg=%"PRIu32", die=%"PRIu32", zone=%d, uj=%"PRIu64"\n",
              pkg, die, zone, uj);
  return uj / 1000000.0;
}
