/**
 * Implementation that wraps Intel Power Gadget's EnergyLib C interface.
 *
 * @author Connor Imes
 * @date 2017-05-26
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "raplcap.h"
#include "raplcap-wrappers.h"
#include "raplcap-common.h"
#ifdef _WIN32
#include <wchar.h>
#include <Windows.h>
#define MSR_FUNC_POWER_ENERGY 1
#define MSR_FUNC_LIMIT 3
#if _M_X64
  #define WIN_ENERGY_LIB_NAME "EnergyLib64"
#else
  #define WIN_ENERGY_LIB_NAME "EnergyLib32"
#endif
#else // OSX
#include <IntelPowerGadget/EnergyLib.h>
#endif

#define MSR_FUNC_N_RESULTS_MAX 3
#define MSR_FUNC_N_RESULTS_POWER_ENERGY 3
#define MSR_FUNC_N_RESULTS_POWER_LIMIT 1
#define POWER_IDX_AVERAGE_POWER 0
#define POWER_IDX_ENERGY_JOULES 1
#define POWER_IDX_ENERGY_MWH 2
#define POWER_LIMIT_IDX_POWER_WATTS 0

// Use function pointers - In Windows, can't link with DLL before runtime
typedef bool (*IPGInitialize) ();
typedef bool (*IPGGetNumNodes) (int* nNodes);
typedef bool (*IPGGetNumMsrs) (int* nMsr);
typedef bool (*IPGGetMsrFunc) (int iMsr, int* funcID);
typedef bool (*IPGReadSample) ();
typedef bool (*IPGGetPowerData) (int iNode, int iMSR, double* result, int* nResult);

typedef struct raplcap_ipg {
#ifdef _WIN32
  HMODULE hMod;
#endif
  IPGInitialize pInitialize;
  IPGGetNumNodes pGetNumNodes;
  IPGGetNumMsrs pGetNumMsrs;
  IPGGetMsrFunc pGetMsrFunc;
  IPGReadSample pReadSample;
  IPGGetPowerData pGetPowerData;
  uint32_t n_pkg;
  int msr_pkg_power_energy;
  int msr_pkg_power_limit;
} raplcap_ipg;

static raplcap rc_default;

// Get the library (on Windows) and set function pointers
static int getEnergyLib(raplcap_ipg* state) {
  assert(state != NULL);
#ifdef _WIN32
  size_t n;
  char lib_path[2048];
  // check IPG's environment variable (set when it was installed)
  const wchar_t* env_dir = _wgetenv(L"IPG_Dir");
  if (env_dir == NULL || wcslen(env_dir) == 0) {
    raplcap_log(DEBUG, "getEnergyLib: IPG_Dir env var not set\n");
    state->hMod = NULL;
  } else {
    raplcap_log(DEBUG, "getEnergyLib: Trying to load "WIN_ENERGY_LIB_NAME" from IPG_Dir env var\n");
    wcstombs_s(&n, lib_path, sizeof(lib_path), env_dir, sizeof(lib_path));
    snprintf(lib_path + n, sizeof(lib_path) - n, "\\%s", WIN_ENERGY_LIB_NAME);
    state->hMod = LoadLibrary(lib_path);
  }
  if (state->hMod == NULL) {
    raplcap_log(DEBUG, "getEnergyLib: Trying to load "WIN_ENERGY_LIB_NAME".dll from standard locations\n");
    // try the current directory and default search paths (system directories, PATH env var directories)
    state->hMod = LoadLibrary(WIN_ENERGY_LIB_NAME);
  }
  if (state->hMod == NULL) {
    raplcap_log(ERROR, "getEnergyLib: Failed to find or load "WIN_ENERGY_LIB_NAME".dll - are IPG_Dir or PATH set?\n");
    return -1;
  }
  raplcap_log(DEBUG, "getEnergyLib: Loaded "WIN_ENERGY_LIB_NAME".dll\n");
  state->pInitialize = (IPGInitialize) GetProcAddress(state->hMod, "IntelEnergyLibInitialize");
  state->pGetNumNodes = (IPGGetNumNodes) GetProcAddress(state->hMod, "GetNumNodes");
  state->pGetMsrFunc = (IPGGetMsrFunc) GetProcAddress(state->hMod, "GetMsrFunc");
  state->pReadSample = (IPGReadSample) GetProcAddress(state->hMod, "ReadSample");
  state->pGetPowerData = (IPGGetPowerData) GetProcAddress(state->hMod, "GetPowerData");
  state->pGetNumMsrs = (IPGGetNumMsrs) GetProcAddress(state->hMod, "GetNumMsrs");
#else
  state->pInitialize = IntelEnergyLibInitialize;
  state->pGetNumNodes = GetNumNodes;
  state->pGetMsrFunc = GetMsrFunc;
  state->pReadSample = ReadSample;
  state->pGetPowerData = GetPowerData;
  state->pGetNumMsrs = GetNumMsrs;
#endif
  return 0;
}

static int initEnergyLib(raplcap_ipg* state, int* nNodes) {
  assert(state != NULL);
  assert(nNodes != NULL);
  int funcID = -1;
  int nMsrs = -1;
  int i;
  // initialize library
  if (!state->pInitialize()) {
    raplcap_log(ERROR, "initEnergyLib: IntelEnergyLibInitialize\n");
    return -1;
  }
  raplcap_log(DEBUG, "initEnergyLib: Initialized EnergyLib\n");
  // get the MSRs (note: not the number of cores, as we sometimes think of it in Linux, but the actual register count)
  if (!state->pGetNumMsrs(&nMsrs) || nMsrs <= 0) {
    raplcap_log(ERROR, "initEnergyLib: GetNumMsrs\n");
    return -1;
  }
  raplcap_log(DEBUG, "initEnergyLib: Found %d MSRs\n", nMsrs);
  // locate the MSR we're interested in
  state->msr_pkg_power_limit = -1;
  for (i = 0; i < nMsrs; i++) {
    if (!state->pGetMsrFunc(i, &funcID)) {
      raplcap_log(ERROR, "initEnergyLib: GetMsrFunc\n");
      return -1;
    }
    if (funcID == MSR_FUNC_POWER_ENERGY) {
      state->msr_pkg_power_energy = i;
    } else if (funcID == MSR_FUNC_LIMIT) {
      state->msr_pkg_power_limit = i;
    }
  }
  if (state->msr_pkg_power_energy < 0 || state->msr_pkg_power_limit < 0) {
    raplcap_log(ERROR, "initEnergyLib: Failed to locate 'Power' or 'Package Power Limit' MSRs\n");
    return -1;
  }
  raplcap_log(DEBUG, "initEnergyLib: Found Package Power Limit MSR: %d\n", state->msr_pkg_power_limit);
  // get the number of packages
  *nNodes = -1;
  if (!state->pGetNumNodes(nNodes) || *nNodes <= 0) {
    raplcap_log(ERROR, "initEnergyLib: GetNumNodes\n");
    return -1;
  }
  raplcap_log(DEBUG, "initEnergyLib: Found %d nodes\n", *nNodes);
  return 0;
}

int raplcap_init(raplcap* rc) {
  raplcap_ipg* state;
  int nNodes;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_ipg*) malloc(sizeof(raplcap_ipg))) == NULL) {
    return -1;
  }
  rc->state = state;
  if (getEnergyLib(state) || initEnergyLib(state, &nNodes)) {
    raplcap_destroy(rc);
    return -1;
  }
  assert(nNodes > 0);
  state->n_pkg = (uint32_t) nNodes;
  rc->nsockets = state->n_pkg;
  raplcap_log(DEBUG, "raplcap_init: Initialized\n");
  return 0;
}

int raplcap_destroy(raplcap* rc) {
  raplcap_ipg* state;
  if (rc == NULL) {
    rc = &rc_default;
  }
  state = (raplcap_ipg*) rc->state;
#ifdef _WIN32
  if (state != NULL && state->hMod != NULL) {
    FreeLibrary(state->hMod);
    raplcap_log(DEBUG, "raplcap_destroy: Freed library handler\n");
  }
#endif
  free(state);
  rc->state = NULL;
  rc->nsockets = 0;
  raplcap_log(DEBUG, "raplcap_destroy: Destroyed\n");
  return 0;
}

uint32_t raplcap_get_num_packages(const raplcap* rc) {
  raplcap tmp;
  raplcap_ipg* state;
  uint32_t n_pkg;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_ipg*) rc->state) != NULL) {
    n_pkg = state->n_pkg;
  } else {
    // Can't discover sockets without initializing an IPG.
    // Can't init a const parameter, and can't init the default impl, o/w a later init could cause memory leak.
    // Instead, we create and destroy a local instance.
    if (raplcap_init(&tmp)) {
      return 0;
    }
    state = (raplcap_ipg*) tmp.state;
    n_pkg = state->n_pkg;
    // destroy never fails...
    raplcap_destroy(&tmp);
  }
  return n_pkg;
}

uint32_t raplcap_get_num_die(const raplcap* rc, uint32_t pkg) {
  errno = ENOSYS;
  return 0;
}

static raplcap_ipg* get_state(const raplcap* rc, uint32_t pkg, uint32_t die) {
  raplcap_ipg* state;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_ipg*) rc->state) == NULL) {
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
  if (die != 0) {
    raplcap_log(ERROR, "Multi-die systems not supported\n");
    errno = ENOSYS;
    return NULL;
  }
  return state;
}

int raplcap_pd_is_zone_supported(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  if (get_state(rc, pkg, die) == NULL) {
    errno = EINVAL;
    return -1;
  }
  // only package is supported
  return zone == RAPLCAP_ZONE_PACKAGE;
}

int raplcap_pd_is_constraint_supported(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                                       raplcap_constraint constraint) {
  if (get_state(rc, pkg, die) == NULL) {
    errno = EINVAL;
    return -1;
  }
  // only package long term constraint is supported
  return zone == RAPLCAP_ZONE_PACKAGE && constraint == RAPLCAP_CONSTRAINT_LONG_TERM;
}

int raplcap_pd_is_zone_enabled(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  // not supported by IPG
  (void) rc;
  (void) pkg;
  (void) die;
  (void) zone;
  errno = ENOSYS;
  return -1;
}

int raplcap_pd_set_zone_enabled(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone, int enabled) {
  // not supported by IPG
  (void) rc;
  (void) pkg;
  (void) die;
  (void) zone;
  (void) enabled;
  errno = ENOSYS;
  return -1;
}

int raplcap_pd_get_limits(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                          raplcap_limit* limit_long, raplcap_limit* limit_short) {
  int nResult = 0;
  double data[MSR_FUNC_N_RESULTS_MAX] = { 0 };
  raplcap_ipg* state;
  if (raplcap_pd_is_zone_supported(rc, pkg, die, zone) <= 0) {
    return -1;
  }
  // will not be NULL if zone is supported
  state = get_state(rc, pkg, die);
  // try twice to work around overflow which doesn't affect our functions
  if (!state->pReadSample() || !state->pReadSample()) {
    raplcap_log(ERROR, "raplcap_pd_get_limits: ReadSample\n");
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_get_limits: Read sample from MSR\n");
  if (!state->pGetPowerData((int) pkg, state->msr_pkg_power_limit, data, &nResult) ||
      nResult != MSR_FUNC_N_RESULTS_POWER_LIMIT) {
    raplcap_log(ERROR, "raplcap_pd_get_limits: GetPowerData\n");
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_get_limits: Got package power limit: %.12f\n", data[POWER_LIMIT_IDX_POWER_WATTS]);
  // TODO: assuming this is long_term data
  // TODO: What about data we can't collect, like long term seconds or any short term data? Currently setting to 0.
  if (limit_long != NULL) {
    limit_long->watts = data[POWER_LIMIT_IDX_POWER_WATTS];
    limit_long->seconds = 0;
  }
  if (limit_short != NULL) {
    limit_short->watts = 0;
    limit_short->seconds = 0;
  }
  return 0;
}

int raplcap_pd_set_limits(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                          const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  // not supported by IPG
  (void) rc;
  (void) pkg;
  (void) die;
  (void) zone;
  (void) limit_long;
  (void) limit_short;
  errno = ENOSYS;
  return -1;
}

int raplcap_pd_get_limit(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                         raplcap_constraint constraint, raplcap_limit* limit) {
  switch (constraint) {
    case RAPLCAP_CONSTRAINT_LONG_TERM:
      return raplcap_pd_get_limits(rc, pkg, die, zone, limit, NULL);
    case RAPLCAP_CONSTRAINT_SHORT_TERM:
      return raplcap_pd_get_limits(rc, pkg, die, zone, NULL, limit);
    case RAPLCAP_CONSTRAINT_PEAK_POWER:
      // not supported by IPG
      errno = ENOSYS;
      break;
  }
  return -1;
}

int raplcap_pd_set_limit(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                         raplcap_constraint constraint, const raplcap_limit* limit) {
  // not supported by IPG
  (void) rc;
  (void) pkg;
  (void) die;
  (void) zone;
  (void) constraint;
  (void) limit;
  errno = ENOSYS;
  return -1;
}

double raplcap_pd_get_energy_counter(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  int nResult = 0;
  double data[MSR_FUNC_N_RESULTS_MAX] = { 0 };
  raplcap_ipg* state;
  if (raplcap_pd_is_zone_supported(rc, pkg, die, zone) <= 0) {
    return -1;
  }
  // will not be NULL if zone is supported
  state = get_state(rc, pkg, die);
  // try twice to work around overflow
  if (!state->pReadSample() || !state->pReadSample()) {
    raplcap_log(ERROR, "raplcap_pd_get_energy_counter: ReadSample\n");
    return -1;
  }
  raplcap_log(DEBUG, "raplcap_pd_get_energy_counter: Read sample from MSR\n");
  if (!state->pGetPowerData((int) pkg, state->msr_pkg_power_energy, data, &nResult) ||
      nResult != MSR_FUNC_N_RESULTS_POWER_ENERGY) {
    raplcap_log(ERROR, "raplcap_pd_get_energy_counter: GetPowerData\n");
    return -1;
  }
  return data[POWER_IDX_ENERGY_JOULES];
}

double raplcap_pd_get_energy_counter_max(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone) {
  (void) rc;
  (void) pkg;
  (void) die;
  (void) zone;
  errno = ENOSYS;
  return -1;
}
