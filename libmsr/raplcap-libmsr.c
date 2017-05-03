/**
 * Implementation that wraps libmsr.
 *
 * @author Connor Imes
 * @date 2016-05-13
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raplcap.h"
#include "raplcap-libmsr.h"
// libmsr headers
#include <msr_core.h>
#include <msr_rapl.h>

typedef struct raplcap_libmsr {
  struct rapl_data* rdata;
} raplcap_libmsr;

static raplcap_libmsr global_state;
// keep track of how many callers we have so we don't init/destroy at wrong times
static int global_count = 0;
static int lock = 0;

static raplcap rc_default;

// by default, assume we need to manage the lifecycle
// if other code also uses libmsr, the app must be aware of it and configure us as needed
#ifndef RAPLCAP_LIBMSR_DO_LIFECYCLE
  #define RAPLCAP_LIBMSR_DO_LIFECYCLE 1
#endif
static int manage_lifecycle = RAPLCAP_LIBMSR_DO_LIFECYCLE;

static int maybe_lifecycle_init(void) {
  int ret = 0;
  // libmsr semantics force us to keep a global state rather than encapsulating it in the caller's struct
  // use global state to track if we've really initialized though - helps avoid improper management of global count
  while (__sync_lock_test_and_set(&lock, 1)) {
    while (lock);
  }
  if (manage_lifecycle && global_count++ == 0) {
    // init msr and rapl
    ret = init_msr() || rapl_init(&global_state.rdata, NULL);
    if (ret) {
      // initialization failed, allow somebody to retry later
      global_count = 0;
    }
  }
  __sync_lock_release(&lock);
  return ret;
}

static int maybe_lifecycle_finish(void) {
  int ret = 0;
  while (__sync_lock_test_and_set(&lock, 1)) {
    while (lock);
  }
  if (manage_lifecycle && --global_count == 0) {
    // cleanup
    ret = finalize_msr();
  }
  __sync_lock_release(&lock);
  return ret;
}

static void raplcap_to_msr(const raplcap_limit* pl, struct rapl_limit* rl) {
  assert(rl != NULL);
  if (pl != NULL) {
    rl->bits = 0;
    if (pl->watts != 0) {
      rl->watts = pl->watts;
    }
    if (pl->seconds != 0) {
      rl->seconds = pl->seconds;
    }
  }
}

static void msr_to_raplcap(const struct rapl_limit* rl, raplcap_limit* pl) {
  assert(rl != NULL);
  if (pl != NULL) {
    pl->seconds = rl->seconds;
    pl->watts = rl->watts;
  }
}

void raplcap_libmsr_set_manage_lifecycle(int is_manage_lifecycle) {
  while (__sync_lock_test_and_set(&lock, 1)) {
    while (lock);
  }
  manage_lifecycle = is_manage_lifecycle;
  __sync_lock_release(&lock);
}

int raplcap_init(raplcap* rc) {
  uint64_t sockets;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state == &global_state) {
    errno = EINVAL;
    return -1;
  }
  errno = 0;
  sockets = num_sockets();
  if (sockets == 0) {
    if (!errno) {
      // best guess is that some type of I/O error occurred
      errno = EIO;
    }
    return -1;
  }
  if (sockets > UINT32_MAX) {
    // totally unexpected, but we shouldn't proceed
    errno = EOVERFLOW;
    return -1;
  }
  if (maybe_lifecycle_init()) {
    return -1;
  }
  rc->nsockets = (uint32_t) sockets;
  rc->state = &global_state;
  return 0;
}

int raplcap_destroy(raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != &global_state) {
    errno = EINVAL;
    return -1;
  }
  rc->state = NULL;
  return maybe_lifecycle_finish();
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  return (rc->state == &global_state && rc->nsockets > 0) ? rc->nsockets : num_sockets();
}

static int msr_get_limits(uint32_t socket, raplcap_zone zone, struct rapl_limit* ll, struct rapl_limit* ls) {
  if (ll != NULL) {
    memset(ll, 0, sizeof(struct rapl_limit));
  }
  if (ls != NULL) {
    memset(ls, 0, sizeof(struct rapl_limit));
  }
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      return get_pkg_rapl_limit(socket, ll, ls);
    case RAPLCAP_ZONE_CORE:
      return get_pp_rapl_limit(socket, ll, NULL);
    case RAPLCAP_ZONE_UNCORE:
      return get_pp_rapl_limit(socket, NULL, ll);
    case RAPLCAP_ZONE_DRAM:
      return get_dram_rapl_limit(socket, ll);
    // not yet supported by libmsr
    case RAPLCAP_ZONE_PSYS:
    default:
      errno = EINVAL;
      return -1;
  }
}

int raplcap_is_zone_supported(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  // we have no way to check with libmsr without just trying operations
  return raplcap_is_zone_enabled(socket, rc, zone) < 0 ? 0 : 1;
}

int raplcap_is_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  struct rapl_limit l;
  int ret;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != &global_state || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  // libmsr doesn't provide an interface to determine enabled/disabled, so we check the bits directly
  // we only need to check one limit - the MSR bits for both limits are from the same register
  ret = msr_get_limits(socket, zone, &l, NULL);
  printf("%lX %f %f\n", l.bits, l.watts, l.seconds);
  if (!ret) {
    switch (zone) {
      case RAPLCAP_ZONE_PACKAGE:
      case RAPLCAP_ZONE_PSYS:
        // check that enabled bits (15 and 47) and clamping bits (16 and 48) are set
        ret = (l.bits & (0x1800000018000)) == 0x1800000018000;
        break;
      case RAPLCAP_ZONE_CORE:
      case RAPLCAP_ZONE_UNCORE:
      case RAPLCAP_ZONE_DRAM:
        // check that enabled bit 15 and clamping bit 16 are set
        ret = (l.bits & 0x18000) == 0x18000;
        break;
      default:
        errno = EINVAL;
        ret = -1;
        break;
    }
  }
  return ret;
}

int raplcap_set_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone, int enabled) {
  // TODO: We can enable a zone by simply writing back its current values, but there's no way to disable it...
  (void) socket;
  (void) rc;
  (void) zone;
  (void) enabled;
  errno = ENOSYS;
  return -1;
}

int raplcap_get_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  struct rapl_limit ll, ls;
  int ret;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != &global_state || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  ret = msr_get_limits(socket, zone, &ll, &ls);
  if (!ret) {
    switch (zone) {
      case RAPLCAP_ZONE_PACKAGE:
      case RAPLCAP_ZONE_PSYS:
        // short term constraint currently only supported in PACKAGE and PSYS
        msr_to_raplcap(&ls, limit_short);
        // fall through to get long term constraint
      case RAPLCAP_ZONE_CORE:
      case RAPLCAP_ZONE_UNCORE:
      case RAPLCAP_ZONE_DRAM:
        msr_to_raplcap(&ll, limit_long);
        break;
      default:
        errno = EINVAL;
        ret = -1;
        break;
    }
  }
  return ret;
}

static int has_empty_field(const raplcap_limit* l) {
  // NULL values do not have empty fields - they are ignored
  return l == NULL ? 0 : (l->watts == 0 || l->seconds == 0);
}

static int msr_set_limits(uint32_t socket, raplcap_zone zone, struct rapl_limit* ll, struct rapl_limit* ls) {
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      return set_pkg_rapl_limit(socket, ll, ls);
    case RAPLCAP_ZONE_CORE:
      return set_pp_rapl_limit(socket, ll, NULL);
    case RAPLCAP_ZONE_UNCORE:
      return set_pp_rapl_limit(socket, NULL, ll);
    case RAPLCAP_ZONE_DRAM:
      return set_dram_rapl_limit(socket, ll);
    // not yet supported by libmsr
    case RAPLCAP_ZONE_PSYS:
    default:
      errno = EINVAL;
      return -1;
  }
}

int raplcap_set_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  struct rapl_limit ll, ls;
  int ret = 0;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != &global_state || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  // first get values to fill any empty ones; no harm done if the zone doesn't actually use both constraints
  if (has_empty_field(limit_long) || has_empty_field(limit_short)) {
    ret = msr_get_limits(socket, zone, &ll, &ls);
  }
  if (!ret) {
    raplcap_to_msr(limit_long, &ll);
    raplcap_to_msr(limit_short, &ls);
    ret = msr_set_limits(socket, zone, limit_long == NULL ? NULL : &ll, limit_short == NULL ? NULL : &ls);
  }
  return ret;
}
