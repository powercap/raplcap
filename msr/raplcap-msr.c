/**
 * Implementation that uses MSRs directly.
 *
 * See the Intel 64 and IA-32 Architectures Software Developer's Manual for MSR
 * register bit fields.
 *
 * @author Connor Imes
 * @date 2016-10-19
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "raplcap.h"
#include "raplcap-common.h"
#include "raplcap-msr.h"
#include "raplcap-msr-common.h"
#include "raplcap-msr-sys.h"
#include "raplcap-wrappers.h"

typedef struct raplcap_msr {
  // assuming consistent unit values between packages
  raplcap_msr_ctx ctx;
  raplcap_msr_sys_ctx* sys;
} raplcap_msr;

static raplcap rc_default;

static const off_t ZONE_OFFSETS_PL[RAPLCAP_NZONES] = {
  MSR_PKG_POWER_LIMIT,      // RAPLCAP_ZONE_PACKAGE
  MSR_PP0_POWER_LIMIT,      // RAPLCAP_ZONE_CORE
  MSR_PP1_POWER_LIMIT,      // RAPLCAP_ZONE_UNCORE
  MSR_DRAM_POWER_LIMIT,     // RAPLCAP_ZONE_DRAM
  MSR_PLATFORM_POWER_LIMIT  // RAPLCAP_ZONE_PSYS
};

static const off_t ZONE_OFFSETS_ENERGY[RAPLCAP_NZONES] = {
  MSR_PKG_ENERGY_STATUS,      // RAPLCAP_ZONE_PACKAGE
  MSR_PP0_ENERGY_STATUS,      // RAPLCAP_ZONE_CORE
  MSR_PP1_ENERGY_STATUS,      // RAPLCAP_ZONE_UNCORE
  MSR_DRAM_ENERGY_STATUS,     // RAPLCAP_ZONE_DRAM
  MSR_PLATFORM_ENERGY_COUNTER // RAPLCAP_ZONE_PSYS
};

static off_t zone_to_msr_offset(raplcap_zone zone, const off_t* offsets) {
  assert(offsets != NULL);
  if ((int) zone < 0 || (int) zone >= RAPLCAP_NZONES) {
    errno = EINVAL;
    return -1;
  }
  return offsets[zone];
}

int raplcap_init(raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  raplcap_msr* state;
  uint64_t msrval;
  uint32_t cpu_model;
  uint32_t n_pkg;
  uint32_t n_die;
  int err_save;
  // check that we recognize the CPU
  if ((cpu_model = msr_get_supported_cpu_model()) == 0) {
    errno = ENOTSUP;
    return -1;
  }
  if ((state = malloc(sizeof(*state))) == NULL) {
    return -1;
  }
  if ((state->sys = msr_sys_init(&n_pkg, &n_die)) == NULL) {
    free(state);
    return -1;
  }
  rc->nsockets = n_pkg;
  rc->state = state;
  if (msr_sys_read(state->sys, &msrval, 0, 0, MSR_RAPL_POWER_UNIT)) {
    err_save = errno;
    raplcap_destroy(rc);
    errno = err_save;
    return -1;
  }
  // now populate context with unit conversions and function pointers
  msr_get_context(&state->ctx, cpu_model, msrval);
  raplcap_log(DEBUG, "raplcap_init: Initialized\n");
  return 0;
}

int raplcap_destroy(raplcap* rc) {
  raplcap_msr* state;
  int ret = 0;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_msr*) rc->state) != NULL) {
    ret = msr_sys_destroy(state->sys);
    free(state);
    rc->state = NULL;
  }
  rc->nsockets = 0;
  raplcap_log(DEBUG, "raplcap_destroy: Destroyed\n");
  return ret;
}

uint32_t raplcap_get_num_packages(const raplcap* rc) {
  const raplcap_msr* state;
  const raplcap_msr_sys_ctx* sys;
  uint32_t n_pkg;
  uint32_t n_die;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_msr*) rc->state) != NULL) {
    sys = state->sys;
  } else {
    sys = NULL;
  }
  return msr_sys_get_num_pkg_die(sys, &n_pkg, &n_die) ? 0 : n_pkg;
}

uint32_t raplcap_get_num_die(const raplcap* rc, uint32_t pkg) {
  const raplcap_msr* state;
  const raplcap_msr_sys_ctx* sys;
  uint32_t n_pkg;
  uint32_t n_die;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_msr*) rc->state) != NULL) {
    sys = state->sys;
  } else {
    sys = NULL;
  }
  if (msr_sys_get_num_pkg_die(sys, &n_pkg, &n_die)) {
    return 0;
  }
  if (pkg >= n_pkg) {
    raplcap_log(ERROR, "Package %"PRIu32" not in range [0, %"PRIu32")\n", pkg, n_pkg);
    errno = EINVAL;
    return 0;
  }
  return n_die;
}

static raplcap_msr* get_state(const raplcap* rc, uint32_t pkg, uint32_t die) {
  raplcap_msr* state;
  uint32_t n_pkg;
  uint32_t n_die;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_msr*) rc->state) == NULL) {
    // unfortunately can't detect if the context just contains garbage
    raplcap_log(ERROR, "Context is not initialized\n");
    errno = EINVAL;
    return NULL;
  }
  if (msr_sys_get_num_pkg_die(state->sys, &n_pkg, &n_die)) {
    return NULL;
  }
  if (pkg >= n_pkg) {
    raplcap_log(ERROR, "Package %"PRIu32" not in range [0, %"PRIu32")\n", pkg, n_pkg);
    errno = EINVAL;
    return NULL;
  }
  if (die >= n_die) {
    raplcap_log(ERROR, "Die %"PRIu32" not in range [0, %"PRIu32")\n", die, n_die);
    errno = EINVAL;
    return NULL;
  }
  return state;
}

int raplcap_pd_is_zone_supported(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  int ret;
  if (state == NULL || msr < 0) {
    return -1;
  }
  ret = msr_sys_read(state->sys, &msrval, pkg, die, msr) ? 0 : 1;
  raplcap_log(DEBUG, "raplcap_pd_is_zone_supported: pkg=%"PRIu32", die=%"PRIu32", zone=%d, supported=%d\n",
              pkg, die, zone, ret);
  return ret;
}

int raplcap_pd_is_constraint_supported(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                                       raplcap_constraint constraint) {
  const raplcap_msr* state = get_state(rc, pkg, die);
  int ret;
  if (state == NULL) {
    return -1;
  }
  if ((int) constraint < 0 || (int) constraint >= RAPLCAP_NCONSTRAINTS) {
    errno = EINVAL;
    return -1;
  }
  ret = msr_is_constraint_supported(&state->ctx, zone, constraint);
  raplcap_log(DEBUG,
              "raplcap_pd_is_constraint_supported: pkg=%"PRIu32", die=%"PRIu32", zone=%d, constraint=%d, supported=%d\n",
              pkg, die, zone, constraint, ret);
  return ret;
}

int raplcap_pd_is_zone_enabled(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  uint64_t msrval;
  int en[2] = { 1, 1 };
  int ret;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  if (state == NULL || msr < 0 || msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
    return -1;
  }
  msr_is_zone_enabled(&state->ctx, zone, msrval, &en[0], &en[1]);
  ret = en[0] && en[1];
  if (ret && !raplcap_msr_is_zone_clamped(rc, pkg, zone)) {
    raplcap_log(INFO, "Zone is enabled but clamping is not\n");
  }
  raplcap_log(DEBUG, "raplcap_pd_is_zone_enabled: pkg=%"PRIu32", die=%"PRIu32", zone=%d, enabled=%d\n",
              pkg, die, zone, ret);
  return ret;
}

// Enables or disables both the "enabled" and "clamped" bits for all constraints
int raplcap_pd_set_zone_enabled(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone, int enabled) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  int ret;
  raplcap_log(DEBUG, "raplcap_pd_set_zone_enabled: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0 || msr_sys_read(state->sys, &msrval, pkg, 0, msr)) {
    return -1;
  }
  msrval = msr_set_zone_enabled(&state->ctx, zone, msrval, &enabled, &enabled);
  if ((ret = msr_sys_write(state->sys, msrval, pkg, die, msr)) == 0) {
    // try to clamp (not supported by all zones or all CPUs)
    msrval = msr_set_zone_clamped(&state->ctx, zone, msrval, &enabled, &enabled);
    if (msr_sys_write(state->sys, msrval, pkg, die, msr)) {
      raplcap_log(INFO, "Clamping not available for this zone or platform\n");
    }
  }
  return ret;
}

int raplcap_pd_get_limits(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                          raplcap_limit* limit_long, raplcap_limit* limit_short) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_pd_get_limits: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0 || msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
    return -1;
  }
  msr_get_limits(&state->ctx, zone, msrval, limit_long, limit_short);
  return 0;
}

int raplcap_pd_set_limits(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                          const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_pd_set_limits: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0 || msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
    return -1;
  }
  msrval = msr_set_limits(&state->ctx, zone, msrval, limit_long, limit_short);
  return msr_sys_write(state->sys, msrval, pkg, die, msr);
}

int raplcap_pd_get_limit(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                         raplcap_constraint constraint, raplcap_limit* limit) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  int ret = 0;
  raplcap_log(DEBUG, "raplcap_pd_get_limit: pkg=%"PRIu32", die=%"PRIu32", zone=%d, constraint=%d\n",
              pkg, die, zone, constraint);
  if (state == NULL || msr < 0) {
    return -1;
  }
  if ((int) constraint < 0 || (int) constraint >= RAPLCAP_NCONSTRAINTS) {
    errno = EINVAL;
    return -1;
  }
  switch (constraint) {
    case RAPLCAP_CONSTRAINT_LONG_TERM:
      if (msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
        return -1;
      }
      msr_get_limits(&state->ctx, zone, msrval, limit, NULL);
      break;
    case RAPLCAP_CONSTRAINT_SHORT_TERM:
      if (msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
        return -1;
      }
      msr_get_limits(&state->ctx, zone, msrval, NULL, limit);
      break;
    case RAPLCAP_CONSTRAINT_PEAK_POWER:
      if (msr_sys_read(state->sys, &msrval, pkg, die, MSR_VR_CURRENT_CONFIG)) {
        return -1;
      }
      if (limit) {
        limit->watts = msr_get_pl4_limit(&state->ctx, zone, msrval);
        limit->seconds = 0;
      }
      break;
    default:
      // reachable
      errno = EINVAL;
      ret = -1;
      break;
  }
  return ret;
}

int raplcap_pd_set_limit(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                         raplcap_constraint constraint, const raplcap_limit* limit) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  int ret = 0;
  raplcap_log(DEBUG, "raplcap_pd_set_limit: pkg=%"PRIu32", die=%"PRIu32", zone=%d, constraint=%d\n",
              pkg, die, zone, constraint);
  if (state == NULL || msr < 0) {
    return -1;
  }
  if ((int) constraint < 0 || (int) constraint >= RAPLCAP_NCONSTRAINTS) {
    errno = EINVAL;
    return -1;
  }
  switch (constraint) {
    case RAPLCAP_CONSTRAINT_LONG_TERM:
      if (msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
        return -1;
      }
      msrval = msr_set_limits(&state->ctx, zone, msrval, limit, NULL);
      ret = msr_sys_write(state->sys, msrval, pkg, die, msr);
      break;
    case RAPLCAP_CONSTRAINT_SHORT_TERM:
      if (msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
        return -1;
      }
      msrval = msr_set_limits(&state->ctx, zone, msrval, NULL, limit);
      ret = msr_sys_write(state->sys, msrval, pkg, die, msr);
      break;
    case RAPLCAP_CONSTRAINT_PEAK_POWER:
      if (msr_sys_read(state->sys, &msrval, pkg, die, MSR_VR_CURRENT_CONFIG)) {
        return -1;
      }
      if (limit) {
        msrval = msr_set_pl4_limit(&state->ctx, zone, msrval, limit->watts);
        ret = msr_sys_write(state->sys, msrval, pkg, die, MSR_VR_CURRENT_CONFIG);
      }
      break;
    default:
      // unreachable
      errno = EINVAL;
      ret = -1;
      break;
  }
  return ret;
}

double raplcap_pd_get_energy_counter(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_pd_get_energy_counter: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0 || msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
    return -1;
  }
  return msr_get_energy_counter(&state->ctx, msrval, zone);
}

double raplcap_pd_get_energy_counter_max(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_pd_get_energy_counter_max: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0) {
    return -1;
  }
  return msr_get_energy_counter_max(&state->ctx, zone);
}

int raplcap_msr_pd_is_zone_clamped(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  uint64_t msrval;
  int cl[2] = { 1, 1 };
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_msr_pd_is_zone_clamped: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0 || msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
    return -1;
  }
  msr_is_zone_clamped(&state->ctx, zone, msrval, &cl[0], &cl[1]);
  return cl[0] && cl[1];
}

int raplcap_msr_is_zone_clamped(const raplcap* rc, uint32_t pkg, raplcap_zone zone) {
  return raplcap_msr_pd_is_zone_clamped(rc, pkg, 0, zone);
}

int raplcap_msr_pd_set_zone_clamped(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone, int clamped) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_msr_pd_set_zone_clamped: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0 || msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
    return -1;
  }
  msrval = msr_set_zone_clamped(&state->ctx, zone, msrval, &clamped, &clamped);
  return msr_sys_write(state->sys, msrval, pkg, die, msr);
}

int raplcap_msr_set_zone_clamped(const raplcap* rc, uint32_t pkg, raplcap_zone zone, int clamped) {
  return raplcap_msr_pd_set_zone_clamped(rc, pkg, 0, zone, clamped);
}

int raplcap_msr_pd_is_zone_locked(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_msr_pd_is_zone_locked: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0 || msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
    return -1;
  }
  return msr_is_zone_locked(&state->ctx, zone, msrval);
}

int raplcap_msr_is_zone_locked(const raplcap* rc, uint32_t pkg, raplcap_zone zone) {
  return raplcap_msr_pd_is_zone_locked(rc, pkg, 0, zone);
}

int raplcap_msr_pd_set_zone_locked(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_msr_pd_set_zone_locked: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0 || msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
    return -1;
  }
  msrval = msr_set_zone_locked(&state->ctx, zone, msrval, 1);
  return msr_sys_write(state->sys, msrval, pkg, die, msr);
}

int raplcap_msr_set_zone_locked(const raplcap* rc, uint32_t pkg, raplcap_zone zone) {
  return raplcap_msr_pd_set_zone_locked(rc, pkg, 0, zone);
}

int raplcap_msr_pd_is_locked(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                             raplcap_constraint constraint) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  int ret;
  raplcap_log(DEBUG, "raplcap_msr_pd_is_locked: pkg=%"PRIu32", die=%"PRIu32", zone=%d, constraint=%d\n",
              pkg, die, zone, constraint);
  if (state == NULL || msr < 0) {
    return -1;
  }
  if ((int) constraint < 0 || (int) constraint >= RAPLCAP_NCONSTRAINTS) {
    errno = EINVAL;
    return -1;
  }
  switch (constraint) {
    case RAPLCAP_CONSTRAINT_LONG_TERM:
    case RAPLCAP_CONSTRAINT_SHORT_TERM:
      if (msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
        return -1;
      }
      ret = msr_is_zone_locked(&state->ctx, zone, msrval);
      break;
    case RAPLCAP_CONSTRAINT_PEAK_POWER:
      if (msr_sys_read(state->sys, &msrval, pkg, die, MSR_VR_CURRENT_CONFIG)) {
        return -1;
      }
      ret = msr_is_pl4_locked(&state->ctx, zone, msrval);
      break;
    default:
      // unreachable
      errno = EINVAL;
      ret = -1;
      break;
  }
  return ret;
}

int raplcap_msr_pd_set_locked(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                              raplcap_constraint constraint) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  int ret;
  raplcap_log(DEBUG, "raplcap_msr_pd_set_locked: pkg=%"PRIu32", die=%"PRIu32", zone=%d, constraint=%d\n",
              pkg, die, zone, constraint);
  if (state == NULL || msr < 0) {
    return -1;
  }
  if ((int) constraint < 0 || (int) constraint >= RAPLCAP_NCONSTRAINTS) {
    errno = EINVAL;
    return -1;
  }
  switch (constraint) {
    case RAPLCAP_CONSTRAINT_LONG_TERM:
    case RAPLCAP_CONSTRAINT_SHORT_TERM:
      if (msr_sys_read(state->sys, &msrval, pkg, die, msr)) {
        return -1;
      }
      msrval = msr_set_zone_locked(&state->ctx, zone, msrval, 1);
      ret = msr_sys_write(state->sys, msrval, pkg, die, msr);
      break;
    case RAPLCAP_CONSTRAINT_PEAK_POWER:
      if (msr_sys_read(state->sys, &msrval, pkg, die, MSR_VR_CURRENT_CONFIG)) {
        return -1;
      }
      msrval = msr_set_pl4_locked(&state->ctx, zone, msrval, 1);
      ret = msr_sys_write(state->sys, msrval, pkg, die, MSR_VR_CURRENT_CONFIG);
      break;
    default:
      // unreachable
      errno = EINVAL;
      ret = -1;
      break;
  }
  return ret;
}

double raplcap_msr_pd_get_time_units(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_msr_pd_get_time_units: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0) {
    return -1;
  }
  return msr_get_time_units(&state->ctx, zone);
}

double raplcap_msr_get_time_units(const raplcap* rc, uint32_t pkg, raplcap_zone zone) {
  return raplcap_msr_pd_get_time_units(rc, pkg, 0, zone);
}

double raplcap_msr_pd_get_power_units(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_msr_pd_get_power_units: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0) {
    return -1;
  }
  return msr_get_power_units(&state->ctx, zone);
}

double raplcap_msr_get_power_units(const raplcap* rc, uint32_t pkg, raplcap_zone zone) {
  return raplcap_msr_pd_get_power_units(rc, pkg, 0, zone);
}

double raplcap_msr_pd_get_energy_units(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  const raplcap_msr* state = get_state(rc, pkg, die);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_msr_pd_get_energy_units: pkg=%"PRIu32", die=%"PRIu32", zone=%d\n", pkg, die, zone);
  if (state == NULL || msr < 0) {
    return -1;
  }
  return msr_get_energy_units(&state->ctx, zone);
}

double raplcap_msr_get_energy_units(const raplcap* rc, uint32_t pkg, raplcap_zone zone) {
  return raplcap_msr_pd_get_energy_units(rc, pkg, 0, zone);
}
