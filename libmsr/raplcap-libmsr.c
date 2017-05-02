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

static int lifecycle_init() {
  int ret = 0;
  // libmsr semantics force us to keep a global state rather than encapsulating it in the caller's struct
  // use global state to track if we've really initialized though - helps avoid improper management of global count
  while (__sync_lock_test_and_set(&lock, 1)) {
    while (lock);
  }
  if (global_count++ == 0) {
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

static int lifecycle_finish() {
  int ret = 0;
  while (__sync_lock_test_and_set(&lock, 1)) {
    while (lock);
  }
  if (--global_count == 0) {
    // cleanup
    ret = finalize_msr() ? -1 : 0;
  }
  __sync_lock_release(&lock);
  return ret;
}

static void raplcap_to_msr(const raplcap_limit* pl, struct rapl_limit* rl) {
  assert(pl != NULL);
  assert(rl != NULL);
  rl->bits = 0;
  rl->watts = pl->watts;
  rl->seconds = pl->seconds;
}

static void msr_to_raplcap(const struct rapl_limit* rl, raplcap_limit* pl) {
  assert(rl != NULL);
  assert(pl != NULL);
  pl->seconds = rl->seconds;
  pl->watts = rl->watts;
}

void raplcap_libmsr_set_manage_lifecycle(int is_manage_lifecycle) {
  manage_lifecycle = is_manage_lifecycle;
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

  if (manage_lifecycle && lifecycle_init()) {
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
  return manage_lifecycle ? lifecycle_finish() : 0;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  return (rc->state == &global_state && rc->nsockets > 0) ? rc->nsockets : num_sockets();
}

int raplcap_is_zone_supported(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  int ret;
  struct rapl_limit rl;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  // we have no way to check with libmsr without just trying operations
  memset(&rl, 0, sizeof(struct rapl_limit));
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      ret = get_pkg_rapl_limit(socket, &rl, NULL);
      break;
    case RAPLCAP_ZONE_CORE:
      ret = get_pp_rapl_limit(socket, &rl, NULL);
      break;
    case RAPLCAP_ZONE_UNCORE:
      ret = get_pp_rapl_limit(socket, NULL, &rl);
      break;
    case RAPLCAP_ZONE_DRAM:
      ret = get_dram_rapl_limit(socket, &rl);
      break;
    case RAPLCAP_ZONE_PSYS:
      // not yet supported by libmsr
      ret = -1;
      break;
    default:
      errno = EINVAL;
      return -1;
  }
  return ret ? 0 : 1;
}

int raplcap_is_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  // TODO
  (void) socket;
  (void) rc;
  (void) zone;
  errno = ENOSYS;
  return -1;
}

int raplcap_set_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone, int enabled) {
  // TODO
  (void) socket;
  (void) rc;
  (void) zone;
  (void) enabled;
  errno = ENOSYS;
  return -1;
}

int raplcap_get_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  struct rapl_limit l0, l1;
  int ret;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != &global_state || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  memset(&l0, 0, sizeof(struct rapl_limit));
  memset(&l1, 0, sizeof(struct rapl_limit));
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      ret = get_pkg_rapl_limit(socket, &l0, &l1);
      // short term constraint currently only supported in PACKAGE
      if (!ret && limit_short != NULL) {
        msr_to_raplcap(&l1, limit_short);
      }
      break;
    case RAPLCAP_ZONE_CORE:
      ret = get_pp_rapl_limit(socket, &l0, NULL);
      break;
    case RAPLCAP_ZONE_UNCORE:
      ret = get_pp_rapl_limit(socket, NULL, &l0);
      break;
    case RAPLCAP_ZONE_DRAM:
      ret = get_dram_rapl_limit(socket, &l0);
      break;
    case RAPLCAP_ZONE_PSYS:
    default:
      errno = EINVAL;
      ret = -1;
      break;
  }
  if (!ret && limit_long != NULL) {
    msr_to_raplcap(&l0, limit_long);
  }
  return ret ? -1 : 0;
}

static void alternative_if_zero(double* dest, double alternative) {
  assert(dest != NULL);
  if (*dest == 0) {
    *dest = alternative;
  }
}

int raplcap_set_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  struct rapl_limit l0;
  struct rapl_limit l1;
  struct rapl_limit* r0 = NULL;
  struct rapl_limit* r1 = NULL;
  raplcap_limit c0, c1;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != &global_state || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  // first get values to fill in empty ones
  // TODO: Only make this call when we have to (i.e. some time or power values are actually 0)
  if (raplcap_get_limits(socket, rc, zone, &c0, &c1)) {
    return -1;
  }
  if (limit_long != NULL) {
    raplcap_to_msr(limit_long, &l0);
    alternative_if_zero(&l0.watts, c0.watts);
    alternative_if_zero(&l0.seconds, c0.seconds);
    r0 = &l0;
  }
  if (limit_short != NULL && zone == RAPLCAP_ZONE_PACKAGE) {
    raplcap_to_msr(limit_short, &l1);
    alternative_if_zero(&l1.watts, c1.watts);
    alternative_if_zero(&l1.seconds, c1.seconds);
    r1 = &l1;
  }
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      return set_pkg_rapl_limit(socket, r0, r1) ? -1 : 0;
    case RAPLCAP_ZONE_CORE:
      return set_pp_rapl_limit(socket, r0, NULL) ? -1 : 0;
    case RAPLCAP_ZONE_UNCORE:
      return set_pp_rapl_limit(socket, NULL, r0) ? -1 : 0;
    case RAPLCAP_ZONE_DRAM:
      return set_dram_rapl_limit(socket, r0) ? -1 : 0;
    case RAPLCAP_ZONE_PSYS:
    default:
      errno = EINVAL;
      return -1;
  }
}
