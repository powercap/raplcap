/**
 * Implementation that wraps libpowercap.
 *
 * @author Connor Imes
 * @date 2021-03-28
 */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "raplcap.h"
#include "raplcap-wrappers.h"
#define RAPLCAP_IMPL "raplcap-powercap"
#include "raplcap-common.h"

// powercap headers
#include <powercap.h>
#include <powercap-sysfs.h>
#include <powercap-tree.h>

#ifndef NAME_MAX_SIZE
#define NAME_MAX_SIZE 64
#endif

#define MAX_ZONE_DEPTH 2

// TODO: currently an undocumented feature
#define ENV_RAPLCAP_POWERCAP_CONTROL_TYPE "RAPLCAP_POWERCAP_CONTROL_TYPE"
#define POWERCAP_CONTROL_TYPE_INTEL_RAPL "intel-rapl"
#define POWERCAP_CONTROL_TYPE_INTEL_RAPL_MMIO "intel-rapl-mmio"

#define CONSTRAINT_NAME_LONG "long_term"
#define CONSTRAINT_NAME_SHORT "short_term"
#define CONSTRAINT_NAME_PEAK "peak_power"

#define ZONE_NAME_PREFIX_PACKAGE "package-"
#define ZONE_NAME_CORE "core"
#define ZONE_NAME_UNCORE "uncore"
#define ZONE_NAME_DRAM "dram"
#define ZONE_NAME_PSYS "psys"

typedef struct raplcap_powercap_cfg {
  uint32_t max_pkg_count;
  uint32_t max_die_count;
  uint32_t psys_count;
} raplcap_powercap_cfg;

typedef struct raplcap_powercap_zone_id {
  // set for all zones
  uint32_t powercap_zones[MAX_ZONE_DEPTH];
  raplcap_zone zone;
  // only set for parent package zones
  uint32_t pkg;
  uint32_t die;
} raplcap_powercap_zone_id;

typedef struct raplcap_powercap_zone_files {
  powercap_zone zone;
  powercap_constraint constraint_long;
  powercap_constraint constraint_short;
  powercap_constraint constraint_peak;
} raplcap_powercap_zone_files;

typedef struct raplcap_powercap_parent_zone {
  raplcap_powercap_zone_files zones[RAPLCAP_ZONE_PSYS + 1];
} raplcap_powercap_parent_zone;

// TODO: lazy file opening

typedef struct raplcap_powercap {
  raplcap_powercap_cfg cfg;
  powercap_tree_root* root;
  raplcap_powercap_parent_zone* parent_zones;
  uint32_t n_parent_zones;
  int ro;
} raplcap_powercap;

static raplcap rc_default;


static int powercap_close(int fd) {
  return (fd > 0 && close(fd)) ? -1 : 0;
}

static int powercap_control_type_close(powercap_control_type* pct) {
  return powercap_close(pct->enabled);
}

static int powercap_zone_close(powercap_zone* pz) {
  int rc = 0;
  rc |= powercap_close(pz->max_energy_range_uj);
  rc |= powercap_close(pz->energy_uj);
  rc |= powercap_close(pz->max_power_range_uw);
  rc |= powercap_close(pz->power_uw);
  rc |= powercap_close(pz->enabled);
  rc |= powercap_close(pz->name);
  return rc;
}

static int powercap_constraint_close(powercap_constraint* pc) {
  int rc = 0;
  rc |= powercap_close(pc->power_limit_uw);
  rc |= powercap_close(pc->time_window_us);
  rc |= powercap_close(pc->max_power_uw);
  rc |= powercap_close(pc->min_power_uw);
  rc |= powercap_close(pc->max_time_window_us);
  rc |= powercap_close(pc->min_time_window_us);
  rc |= powercap_close(pc->name);
  return rc;
}

static int destroy_all(raplcap_powercap_zone_files* files) {
  assert(files != NULL);
  int ret = 0;
  ret |= powercap_zone_close(&files->zone);
  ret |= powercap_constraint_close(&files->constraint_long);
  ret |= powercap_constraint_close(&files->constraint_short);
  ret |= powercap_constraint_close(&files->constraint_peak);
  return ret;
}

static int parse_pkg_die(const char* name, raplcap_powercap_zone_id* id) {
  char* endptr;
  char* endptr2;
  id->pkg = strtoul(&name[sizeof(ZONE_NAME_PREFIX_PACKAGE) / sizeof(ZONE_NAME_PREFIX_PACKAGE[0]) - 1], &endptr, 0);
  if (!id->pkg && endptr == name) {
    // failed to get pkg ID - something unexpected in name format
    raplcap_log(ERROR, "parse_pkg_die: Failed to parse package from zone name: %s\n", name);
    errno = EINVAL;
    return -1;
  }
  if (*endptr == '\0') {
    // the string format is package-X
    id->die = 0;
  } else if (endptr[1] == '-') {
    // the string format is (presumably) package-X-die-Y
    id->die = strtoul(endptr + 1, &endptr2, 0);
    if (!id->die && (endptr + 1) == endptr2) {
      // failed to get die - something unexpected in name format
      raplcap_log(ERROR, "parse_pkg_die: Failed to parse die from zone name: %s\n", name);
      errno = EINVAL;
      return -1;
    }
  } else {
    raplcap_log(ERROR, "parse_pkg_die: Unsupported zone name format: %s\n", name);
    errno = EINVAL;
    return -1;
  }
  return 0;
}

static int get_id_from_name(const char* control_type, const uint32_t* zones, uint32_t depth, raplcap_powercap_zone_id* id) {
  char name[NAME_MAX_SIZE] = { 0 };
  int ret = 0;
  memcpy(id->powercap_zones, zones, depth * sizeof(*zones));
  if (powercap_sysfs_zone_get_name(control_type, zones, depth, name, sizeof(name)) < 0) {
    raplcap_perror(ERROR, "get_id_from_name: powercap_sysfs_zone_get_name");
    ret = -1;
  } else if (!strncmp(name, ZONE_NAME_PSYS, sizeof(name))) {
    id->zone = RAPLCAP_ZONE_PSYS;
  } else if (!strncmp(name, ZONE_NAME_CORE, sizeof(name))) {
    id->zone = RAPLCAP_ZONE_CORE;
  } else if (!strncmp(name, ZONE_NAME_UNCORE, sizeof(name))) {
    id->zone = RAPLCAP_ZONE_UNCORE;
  } else if (!strncmp(name, ZONE_NAME_DRAM, sizeof(name))) {
    id->zone = RAPLCAP_ZONE_DRAM;
  } else if (!strncmp(name, ZONE_NAME_PREFIX_PACKAGE, sizeof(ZONE_NAME_PREFIX_PACKAGE) - 1)) {
    id->zone = RAPLCAP_ZONE_PACKAGE;
    ret = parse_pkg_die(name, id);
  } else {
    raplcap_log(ERROR, "get_id_from_name: Unsupported zone name format: %s\n", name);
    errno = EINVAL;
    ret = -1;
  }
  return ret;
}

static int tree_cb_get_cfg(const char* control_type, const uint32_t* zones, uint32_t depth, void* ctx, void** ctx_node) {
  assert(ctx != NULL);
  assert(*ctx_node == NULL);
  raplcap_powercap_cfg* c = (raplcap_powercap_cfg*) ctx;
  raplcap_powercap_zone_id* id;
  if ((id = calloc(1, sizeof(*id))) == NULL) {
    return -1;
  }
  if (get_id_from_name(control_type, zones, depth, id)) {
    free(id);
    return -1;
  }
  *ctx_node = id;
  // we only parse parent zones
  if (depth > 1) {
    if (id->zone == RAPLCAP_ZONE_PSYS) {
      c->psys_count++;
    } else {
      if (c->max_pkg_count <= id->pkg) {
        c->max_pkg_count = id->pkg + 1;
      }
      if (c->max_die_count <= id->die) {
        c->max_die_count = id->die + 1;
      }
    }
  }
  return 0;
}

static int tree_cb_init(const char* control_type, const uint32_t* zones, uint32_t depth, void* ctx, void** ctx_node) {
  assert(ctx != NULL);
  raplcap_powercap_zone_id* id;
  raplcap_powercap* state = (raplcap_powercap*) ctx;
  if (depth > 1) {
    // we only care about parent zones
    return 0;
  }
  assert(*ctx_node != NULL);
  id = *(raplcap_powercap_zone_id**)ctx_node;
  free(id);
  // TODO: whatever we need for init...
  return 0;
}

static int tree_cb_cleanup(const char* control_type, const uint32_t* zones, uint32_t depth, void* ctx, void** ctx_node) {
  free(*ctx_node); // raplcap_powercap_zone_id pointer, or NULL
  return 0;
}

static const char* get_control_type(void) {
  const char* env_ct = getenv(ENV_RAPLCAP_POWERCAP_CONTROL_TYPE);
  if (env_ct == NULL || !strcmp(env_ct, POWERCAP_CONTROL_TYPE_INTEL_RAPL)) {
    return POWERCAP_CONTROL_TYPE_INTEL_RAPL;
  }
  if (!strcmp(env_ct, POWERCAP_CONTROL_TYPE_INTEL_RAPL_MMIO)) {
    return POWERCAP_CONTROL_TYPE_INTEL_RAPL_MMIO;
  }
  errno = EINVAL;
  return NULL;
}

int raplcap_init(raplcap* rc) {
  raplcap_powercap* state;
  int err_save;
  const char* env_ro = getenv(ENV_RAPLCAP_READ_ONLY);
  const char* control_type;

  if (rc == NULL) {
    rc = &rc_default;
  }

  if ((control_type = get_control_type()) == NULL) {
    return -1;
  }

  if ((state = calloc(1, sizeof(*state))) == NULL) {
    raplcap_perror(ERROR, "raplcap_init: malloc");
    return -1;
  }
  state->ro = env_ro == NULL ? 0 : atoi(env_ro);
  if (!(state->root = powercap_tree_root_init(control_type))) {
    err_save = errno;
    raplcap_perror(ERROR, "raplcap_init: powercap_tree_root_init");
    free(state);
    errno = err_save;
    return -1;
  }

  if (powercap_tree_root_walk(state->root, tree_cb_get_cfg, &state->cfg)) {
    err_save = errno;
    // TODO: walk to cleanup allocated cfgs
    powercap_tree_root_destroy(state->root);
    free(state);
    errno = err_save;
    return -1;
  }

  // TODO: Need to associate subzones with powercap zones[].

  // TODO: verify that parent zones are actually found
  if (state->cfg.psys_count > 1) {
    // TODO
  }
  if (!state->cfg.max_pkg_count) {
    // TODO
  }

  // on a system with homogeneous processors, this  allocation size is optimal
  state->n_parent_zones = state->cfg.max_pkg_count * state->cfg.max_die_count + state->cfg.psys_count;
  if ((state->parent_zones = calloc(state->n_parent_zones, sizeof(*state->parent_zones))) == NULL) {
    err_save = errno;
    // TODO: walk to cleanup allocated cfgs
    powercap_tree_root_destroy(state->root);
    free(state);
    errno = err_save;
    return -1;
  }

  // TODO
  if (powercap_tree_root_walk(state->root, tree_cb_init, state)) {
    err_save = errno;
    powercap_tree_root_walk(state->root, tree_cb_cleanup, state);
    powercap_tree_root_destroy(state->root);
    free(state);
    errno = err_save;
    return -1;
  }

  rc->state = state;
  rc->nsockets = 0; // TODO: n_pkg
  raplcap_log(DEBUG, "raplcap_init: Initialized\n");
  return 0;
}

int raplcap_destroy(raplcap* rc) {
  raplcap_powercap* state;
  int err_save = 0;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_powercap*) rc->state) != NULL) {
    if (powercap_tree_root_walk(state->root, tree_cb_cleanup, state)) {
      err_save = errno;
    }
    powercap_tree_root_destroy(state->root);
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
  // TODO
  return 0;
}

uint32_t raplcap_get_num_die(const raplcap* rc, uint32_t pkg) {
  // TODO
  return 0;
}

int raplcap_pd_is_zone_supported(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  // TODO
  return 0;
}

int raplcap_pd_is_zone_enabled(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  // TODO
  return 0;
}

int raplcap_pd_set_zone_enabled(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone, int enabled) {
  // TODO
  return 0;
}

int raplcap_pd_get_limits(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                          raplcap_limit* limit_long, raplcap_limit* limit_short) {
  // TODO
  return 0;
}

int raplcap_pd_set_limits(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                          const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  // TODO
  return 0;
}

double raplcap_pd_get_energy_counter(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  // TODO
  return 0;
}

double raplcap_pd_get_energy_counter_max(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  // TODO
  return 0;
}
