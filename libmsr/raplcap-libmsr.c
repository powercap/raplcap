/**
 * Implementation that wraps libmsr.
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
#define RAPLCAP_IMPL "raplcap-libmsr"
#include "raplcap-common.h"
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
  raplcap_log(DEBUG, "maybe_lifecycle_init: manage_lifecycle=%d\n", manage_lifecycle);
  if (manage_lifecycle && global_count == 0) {
    raplcap_log(DEBUG, "maybe_lifecycle_init: global_count=%d\n", global_count);
    // init msr and rapl
    raplcap_log(INFO, "Initializing global libmsr context\n");
    if ((ret = init_msr()) != 0) {
      raplcap_perror(ERROR, "maybe_lifecycle_init: (libmsr_)init_msr");
    } else if ((ret = rapl_init(&global_state.rdata, NULL)) != 0) {
      raplcap_perror(ERROR, "maybe_lifecycle_init: (libmsr_)rapl_init");
    } else {
      global_count++;
      raplcap_log(DEBUG, "maybe_lifecycle_init: libmsr initialized\n");
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
  raplcap_log(DEBUG, "maybe_lifecycle_finish: manage_lifecycle=%d, global_count=%d\n", manage_lifecycle, global_count);
  if (manage_lifecycle && global_count == 1) {
    raplcap_log(INFO, "Finalizing global libmsr context\n");
    // cleanup
    global_count--;
    if ((ret = finalize_msr()) != 0) {
      raplcap_perror(ERROR, "maybe_lifecycle_finish: (libmsr_)finalize_msr");
    }
  }
  __sync_lock_release(&lock);
  return ret;
}

static void raplcap_to_msr(const raplcap_limit* pl, struct rapl_limit* rl) {
  assert(rl != NULL);
  if (pl != NULL) {
    rl->bits = 0;
    if (!is_zero_dbl(pl->watts)) {
      rl->watts = pl->watts;
    }
    if (!is_zero_dbl(pl->seconds)) {
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

static int check_state(const raplcap* rc, uint32_t socket) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (socket >= rc->nsockets) {
    raplcap_log(ERROR, "check_state: Socket %"PRIu32" not in range [0, %"PRIu32")\n", socket, rc->nsockets);
    errno = EINVAL;
    return -1;
  }
  if (rc->state != &global_state) {
    raplcap_log(ERROR, "check_state: RAPLCap context not initialized with libmsr's context\n");
    errno = EINVAL;
    return -1;
  }
  return 0;
}

void raplcap_libmsr_set_manage_lifecycle(int is_manage_lifecycle) {
  while (__sync_lock_test_and_set(&lock, 1)) {
    while (lock);
  }
  raplcap_log(DEBUG, "raplcap_libmsr_set_manage_lifecycle: old=%d, new=%d\n", manage_lifecycle, is_manage_lifecycle);
  manage_lifecycle = is_manage_lifecycle;
  __sync_lock_release(&lock);
}

static uint32_t get_libmsr_sockets(void) {
  uint64_t sockets;
  errno = 0;
  if ((sockets = num_sockets()) == 0) {
    raplcap_perror(ERROR, "get_libmsr_sockets: (libmsr_)num_sockets");
    if (!errno) {
      // best guess is that some type of I/O error occurred
      errno = EIO;
    }
  } else if (sockets > UINT32_MAX) {
    // totally unexpected, but we shouldn't proceed
    raplcap_log(ERROR, "get_libmsr_sockets: Found too many sockets: %"PRIu64"\n", sockets);
    errno = EOVERFLOW;
    sockets = 0;
  }
  raplcap_log(DEBUG, "get_libmsr_sockets: sockets=%"PRIu64"\n", sockets);
  return (uint32_t) sockets;
}

int raplcap_init(raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state == &global_state) {
    // could be somebody tried to re-initialize the default context - not a failure
    raplcap_log(INFO, "RAPLCap context already initialized with libmsr's context\n");
    return 0;
  }
  if ((rc->nsockets = get_libmsr_sockets()) == 0 || maybe_lifecycle_init()) {
    rc->nsockets = 0;
    return -1;
  }
  rc->state = &global_state;
  raplcap_log(DEBUG, "raplcap_init: Initialized\n");
  return 0;
}

int raplcap_destroy(raplcap* rc) {
  int ret = 0;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != &global_state) {
    // could be somebody tried to re-destroy the default context - not a failure
    raplcap_log(INFO, "RAPLCap context not initialized with libmsr's context\n");
    return 0;
  }
  rc->state = NULL;
  rc->nsockets = 0;
  ret = maybe_lifecycle_finish();
  raplcap_log(DEBUG, "raplcap_destroy: Destroyed\n");
  return ret;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  return (rc->state == &global_state && rc->nsockets > 0) ? rc->nsockets : get_libmsr_sockets();
}

static int msr_get_limits(uint32_t socket, raplcap_zone zone, struct rapl_limit* ll, struct rapl_limit* ls) {
  int ret;
  if (ll != NULL) {
    memset(ll, 0, sizeof(struct rapl_limit));
  }
  if (ls != NULL) {
    memset(ls, 0, sizeof(struct rapl_limit));
  }
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      if ((ret = get_pkg_rapl_limit(socket, ll, ls)) != 0) {
        raplcap_perror(ERROR, "msr_get_limits: (libmsr_)get_pkg_rapl_limit");
      }
      break;
    case RAPLCAP_ZONE_CORE:
#ifdef LIBMSR_PP_SUPPORTED
      if ((ret = get_pp_rapl_limit(socket, ll, NULL)) != 0) {
        raplcap_perror(ERROR, "msr_get_limits: (libmsr_)get_pp_rapl_limit");
      }
#else
      raplcap_log(ERROR, "msr_get_limits: Core zone not supported by libmsr\n");
      errno = ENOTSUP;
      ret = -1;
#endif
      break;
    case RAPLCAP_ZONE_UNCORE:
#ifdef LIBMSR_PP_SUPPORTED
      if ((ret = get_pp_rapl_limit(socket, NULL, ll)) != 0) {
        raplcap_perror(ERROR, "msr_get_limits: (libmsr_)get_pp_rapl_limit");
      }
#else
      raplcap_log(ERROR, "msr_get_limits: Uncore zone not supported by libmsr\n");
      errno = ENOTSUP;
      ret = -1;
#endif
      break;
    case RAPLCAP_ZONE_DRAM:
      if ((ret = get_dram_rapl_limit(socket, ll)) != 0) {
        raplcap_perror(ERROR, "msr_get_limits: (libmsr_)get_dram_rapl_limit");
      }
      break;
    case RAPLCAP_ZONE_PSYS:
      raplcap_log(ERROR, "msr_get_limits: PSys/Platform zone not supported by libmsr\n");
      errno = ENOTSUP;
      ret = -1;
      break;
    default:
      errno = EINVAL;
      ret = -1;
      break;
  }
  if (!ret) {
    if (ll != NULL) {
      raplcap_log(DEBUG, "msr_get_limits: socket=%"PRIu32", zone=%d, long_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                  socket, zone, ll->seconds, ll->watts);
    }
    if (ls != NULL) {
      raplcap_log(DEBUG, "msr_get_limits: socket=%"PRIu32", zone=%d, short_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                  socket, zone, ls->seconds, ls->watts);
    }
  }
  return ret;
}

int raplcap_is_zone_supported(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  // we have no way to check with libmsr without just trying operations
  int ret = raplcap_is_zone_enabled(rc, socket, zone) < 0 ? 0 : 1;
  raplcap_log(DEBUG, "raplcap_is_zone_supported: socket=%"PRIu32", zone=%d, supported=%d\n", socket, zone, ret);
  return ret;
}

int raplcap_is_zone_enabled(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  struct rapl_limit l;
  int ret;
  if (check_state(rc, socket)) {
    return -1;
  }
  // libmsr doesn't provide an interface to determine enabled/disabled, so we check the bits directly
  // we only need to check one limit - the MSR bits for both limits are from the same register
  if ((ret = msr_get_limits(socket, zone, &l, NULL)) == 0) {
    raplcap_log(DEBUG, "raplcap_is_zone_enabled: MSR bits=0x%016lX\n", l.bits);
    switch (zone) {
      case RAPLCAP_ZONE_PACKAGE:
      case RAPLCAP_ZONE_PSYS:
        // check that enabled bits (15 and 47) are set
        ret = (l.bits & (0x800000008000)) == 0x800000008000;
        // check that clamping bits (16 and 48) are set
        if (ret && (l.bits & 0x1000000010000) != 0x1000000010000) {
          raplcap_log(INFO, "Zone is enabled but clamping is not\n");
        }
        break;
      case RAPLCAP_ZONE_CORE:
      case RAPLCAP_ZONE_UNCORE:
      case RAPLCAP_ZONE_DRAM:
        // check that enabled bit (15) is set
        ret = (l.bits & 0x8000) == 0x8000;
        // check that clamping bit (16) is set
        if (ret && (l.bits & 0x10000) != 0x10000) {
          raplcap_log(INFO, "Zone is enabled but clamping is not\n");
        }
        break;
      default:
        errno = EINVAL;
        ret = -1;
        break;
    }
    raplcap_log(DEBUG, "raplcap_is_zone_enabled: socket=%"PRIu32", zone=%d, enabled=%d\n", socket, zone, ret);
  }
  return ret;
}

int raplcap_set_zone_enabled(const raplcap* rc, uint32_t socket, raplcap_zone zone, int enabled) {
  // TODO: We can enable a zone by simply writing back its current values, but there's no way to disable it...
  (void) socket;
  (void) rc;
  (void) zone;
  (void) enabled;
  raplcap_log(ERROR, "raplcap_set_zone_enabled: No libmsr function to enable/disable zones\n");
  raplcap_log(INFO, "raplcap_set_zone_enabled: Hint - use raplcap_set_limits(...) to enable a zone\n");
  errno = ENOSYS;
  return -1;
}

int raplcap_get_limits(const raplcap* rc, uint32_t socket, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  struct rapl_limit ll, ls;
  int ret;
  if (check_state(rc, socket)) {
    return -1;
  }
  if ((ret = msr_get_limits(socket, zone, &ll, &ls)) == 0) {
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
  return l == NULL ? 0 : (is_zero_dbl(l->watts) || is_zero_dbl(l->seconds));
}

static int msr_set_limits(uint32_t socket, raplcap_zone zone, struct rapl_limit* ll, struct rapl_limit* ls) {
  int ret;
  if (ll != NULL) {
    raplcap_log(DEBUG, "msr_set_limits: socket=%"PRIu32", zone=%d, long_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, ll->seconds, ll->watts);
  }
  if (ls != NULL) {
    raplcap_log(DEBUG, "msr_set_limits: socket=%"PRIu32", zone=%d, short_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, ls->seconds, ls->watts);
  }
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      if ((ret = set_pkg_rapl_limit(socket, ll, ls)) != 0) {
        raplcap_perror(ERROR, "msr_set_limits: (libmsr_)set_pkg_rapl_limit");
      }
      break;
    case RAPLCAP_ZONE_CORE:
#ifdef LIBMSR_PP_SUPPORTED
      if ((ret = set_pp_rapl_limit(socket, ll, NULL)) != 0) {
        raplcap_perror(ERROR, "msr_set_limits: (libmsr_)set_pp_rapl_limit");
      }
#else
      raplcap_log(ERROR, "msr_set_limits: Core zone not supported by libmsr\n");
      errno = ENOTSUP;
      ret = -1;
#endif
      break;
    case RAPLCAP_ZONE_UNCORE:
#ifdef LIBMSR_PP_SUPPORTED
      if ((ret = set_pp_rapl_limit(socket, NULL, ll)) != 0) {
        raplcap_perror(ERROR, "msr_set_limits: (libmsr_)set_pp_rapl_limit");
      }
#else
      raplcap_log(ERROR, "msr_set_limits: Core zone not supported by libmsr\n");
      errno = ENOTSUP;
      ret = -1;
#endif
      break;
    case RAPLCAP_ZONE_DRAM:
      if ((ret = set_dram_rapl_limit(socket, ll)) != 0) {
        raplcap_perror(ERROR, "msr_set_limits: (libmsr_)set_dram_rapl_limit");
      }
      break;
    case RAPLCAP_ZONE_PSYS:
      raplcap_log(ERROR, "msr_set_limits: PSys/Platform zone not supported by libmsr\n");
      errno = ENOTSUP;
      ret = -1;
      break;
    default:
      errno = EINVAL;
      ret = -1;
      break;
  }
  return ret;
}

int raplcap_set_limits(const raplcap* rc, uint32_t socket, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  struct rapl_limit ll, ls;
  int ret = 0;
  if (check_state(rc, socket)) {
    return -1;
  }
  // first get values to fill any empty ones; no harm done if the zone doesn't actually use both constraints
  if (has_empty_field(limit_long) || has_empty_field(limit_short)) {
    raplcap_log(DEBUG, "raplcap_set_limits: Empty field(s) found, fetching current MSR value(s) to fill in\n");
    ret = msr_get_limits(socket, zone, &ll, &ls);
  }
  if (!ret) {
    raplcap_to_msr(limit_long, &ll);
    raplcap_to_msr(limit_short, &ls);
    ret = msr_set_limits(socket, zone, limit_long == NULL ? NULL : &ll, limit_short == NULL ? NULL : &ls);
  }
  return ret;
}
