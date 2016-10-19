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

static inline void raplcap_to_msr(const raplcap_limit* pl, struct rapl_limit* rl) {
  assert(pl != NULL);
  assert(rl != NULL);
  rl->bits = 0;
  rl->watts = pl->watts;
  rl->seconds = pl->seconds;
}

static inline void msr_to_raplcap(const struct rapl_limit* rl, raplcap_limit* pl) {
  assert(rl != NULL);
  assert(pl != NULL);
  pl->seconds = rl->seconds;
  pl->watts = rl->watts;
}

int raplcap_init(raplcap* rc) {
  int ret = 0;
  int initialized;
  uint64_t sockets;
  if (rc == NULL || rc->state == &global_state) {
    errno = EINVAL;
    return -1;
  }
  memset(rc, 0, sizeof(raplcap));
  // TODO: We should cache sockets in global state
  sockets = num_sockets();
  if (sockets > UINT32_MAX) {
    // totally unexpected, but we shouldn't proceed
    errno = EOVERFLOW;
    return -1;
  }
  // libmsr semantics force us to keep a global state rather than encapsulating it in the caller's struct
  // use global state to track if we've really initialized though - helps avoid improper management of global count
  while (__sync_lock_test_and_set(&lock, 1)) {
    while (lock);
  }
  initialized = __sync_fetch_and_add(&global_count, 1);
  if (!initialized) {
    // init msr and rapl
    ret = init_msr() || rapl_init(&global_state.rdata, NULL);
    if (ret) {
      // initialization failed, allow somebody to retry later
      global_count = 0;
    }
  }
  __sync_lock_release(&lock);
  if (!ret) {
    rc->nsockets = (uint32_t) sockets;
    rc->state = &global_state;
  }
  return ret ? -1 : 0;
}

int raplcap_destroy(raplcap* rc) {
  int ret = 0;
  int count;
  if (rc == NULL || rc->state != &global_state) {
    errno = EINVAL;
    return -1;
  }
  while (__sync_lock_test_and_set(&lock, 1)) {
    while (lock);
  }
  count = __sync_add_and_fetch(&global_count, -1);
  if (count == 0) {
    // cleanup
    // TODO: In the future rapl_finalize will restore registers, which we don't want
    // It will also clean up memory... but these should be separate operations
    // rapl_finalize();
    ret = finalize_msr() ? -1 : 0;
  }
  __sync_lock_release(&lock);
  rc->state = NULL;
  return ret;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  return (rc == NULL || rc->state != &global_state) ? num_sockets() : rc->nsockets;
}

int raplcap_is_zone_supported(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  if (rc == NULL || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  // we have no way to check with libmsr without just trying operations, so we hardcode responses
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
    case RAPLCAP_ZONE_CORE:
      // always supported
      return 1;
    case RAPLCAP_ZONE_UNCORE:
#if defined(RAPL_UNCORE_SUPPORTED)
      return RAPL_UNCORE_SUPPORTED;
#else
      // TODO
      return 1;
#endif
    case RAPLCAP_ZONE_DRAM:
#if defined(RAPL_DRAM_SUPPORTED)
      return RAPL_DRAM_SUPPORTED;
#else
      // TODO
      return 1;
#endif
    case RAPLCAP_ZONE_PSYS:
#if defined(RAPL_PSYS_SUPPORTED)
      return RAPL_PSYS_SUPPORTED;
#else
      // TODO: not yet supported by libmsr
      return 0;
#endif
    default:
      errno = EINVAL;
      return -1;
  }
}

int raplcap_get_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  struct rapl_limit l0, l1;
  memset(&l0, 0, sizeof(struct rapl_limit));
  memset(&l1, 0, sizeof(struct rapl_limit));
  int ret;
  if (rc == NULL || rc->state != &global_state || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      ret = get_pkg_rapl_limit(socket, &l0, &l1);
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
  if (!ret) {
    if (limit_long != NULL) {
      msr_to_raplcap(&l0, limit_long);
    }
    if (limit_short != NULL) {
      msr_to_raplcap(&l1, limit_short);
    }
  }
  return ret ? -1 : 0;
}

static inline void enforce_not_zero(double* dest, const double* alternative) {
  assert(dest != NULL);
  assert(alternative != NULL);
  assert(*alternative != 0);
  if (*dest == 0) {
    *dest = *alternative;
  }
}

int raplcap_set_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  struct rapl_limit l0;
  struct rapl_limit l1;
  struct rapl_limit* r0 = NULL;
  struct rapl_limit* r1 = NULL;
  struct raplcap_limit c0, c1;
  if (rc == NULL || rc->state != &global_state || socket >= rc->nsockets) {
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
    enforce_not_zero(&l0.watts, &c0.watts);
    enforce_not_zero(&l0.seconds, &c0.seconds);
    r0 = &l0;
  }
  if (limit_short != NULL) {
    raplcap_to_msr(limit_short, &l1);
    enforce_not_zero(&l1.watts, &c1.watts);
    enforce_not_zero(&l1.seconds, &c1.seconds);
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
