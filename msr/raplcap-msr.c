/**
 * Implementation that uses MSRs directly.
 *
 * See the Intel 64 and IA-32 Architectures Software Developer's Manual for MSR
 * register bit fields.
 *
 * @author Connor Imes
 * @date 2016-10-19
 */
// for popen, pread, pwrite, sysconf
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "raplcap.h"
#include "raplcap-common.h"
#include "raplcap-msr.h"
#include "raplcap-msr-common.h"

typedef struct raplcap_msr {
  // assuming consistent unit values between sockets
  raplcap_msr_ctx ctx;
  int* fds;
} raplcap_msr;

static raplcap rc_default;

static int open_msr(uint32_t core, int flags) {
  char msr_filename[32];
  int fd;
  // first try using the msr_safe kernel module
  snprintf(msr_filename, sizeof(msr_filename), "/dev/cpu/%"PRIu32"/msr_safe", core);
  if ((fd = open(msr_filename, flags)) < 0) {
    raplcap_perror(DEBUG, msr_filename);
    raplcap_log(INFO, "msr-safe not available, falling back on standard msr\n");
    // fall back on the standard msr kernel module
    snprintf(msr_filename, sizeof(msr_filename), "/dev/cpu/%"PRIu32"/msr", core);
    if ((fd = open(msr_filename, flags)) < 0) {
      raplcap_perror(ERROR, msr_filename);
      if (errno == ENOENT) {
        raplcap_log(WARN, "Is the msr kernel module loaded?\n");
      }
    }
  }
  return fd;
}

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
    raplcap_log(ERROR, "zone_to_msr_offset: Unknown zone: %d\n", zone);
    errno = EINVAL;
    return -1;
  }
  return offsets[zone];
}

static int read_msr_by_offset(int fd, off_t msr, uint64_t* data, int silent) {
  assert(msr >= 0);
  assert(data != NULL);
  if (pread(fd, data, sizeof(uint64_t), msr) == sizeof(uint64_t)) {
    raplcap_log(DEBUG, "read_msr_by_offset: msr=0x%lX, data=0x%016lX\n", msr, *data);
    return 0;
  }
  if (!silent) {
    raplcap_log(ERROR, "read_msr_by_offset(0x%lX): pread: %s\n", msr, strerror(errno));
  }
  return -1;
}

static int write_msr_by_offset(int fd, off_t msr, uint64_t data, int silent) {
  assert(msr >= 0);
  raplcap_log(DEBUG, "write_msr_by_offset: msr=0x%lX, data=0x%016lX\n", msr, data);
  if (pwrite(fd, &data, sizeof(uint64_t), msr) == sizeof(uint64_t)) {
    return 0;
  }
  if (!silent) {
    raplcap_log(ERROR, "write_msr_by_offset(0x%lX): pwrite: %s\n", msr, strerror(errno));
  }
  return -1;
}

static uint32_t get_cpu_count(void) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n <= 0 || n > UINT32_MAX) {
    errno = ENODEV;
    return 0;
  }
  return (uint32_t) n;
}

static int get_cpu_to_socket_mapping(uint32_t* cpu_to_socket, uint32_t ncpus) {
  // assumes cpus are numbered from 0 to ncpus-1
  assert(cpu_to_socket != NULL);
  assert(ncpus > 0);
  char fname[92] = { 0 };
  FILE* f;
  uint32_t i;
  int fret;
  for (i = 0; i < ncpus; i++) {
    // phys socket IDs may not be in range [0, nsockets), see kernel docs: Documentation/cputopology.txt
    snprintf(fname, sizeof(fname), "/sys/devices/system/cpu/cpu%"PRIu32"/topology/physical_package_id", i);
    if ((f = fopen(fname, "r")) == NULL) {
      raplcap_perror(ERROR, fname);
      return -1;
    }
    fret = fscanf(f, "%"PRIu32, &cpu_to_socket[i]);
    if (fclose(f)) {
      raplcap_perror(WARN, "get_cpu_to_socket_mapping: fclose");
    }
    if (fret != 1) {
      raplcap_log(ERROR, "get_cpu_to_socket_mapping: Failed to read physical_package_id for cpu%"PRIu32"\n", i);
      errno = ENODATA;
      return -1;
    }
    raplcap_log(DEBUG, "get_cpu_to_socket_mapping: cpu=%"PRIu32", phys_socket=%"PRIu32"\n", i, cpu_to_socket[i]);
  }
  return 0;
}

static int cmp_u32(const void* a, const void* b) {
  return *((const uint32_t*) a) > *((const uint32_t*) b) ? 1 :
         ((*((const uint32_t*) a) < *((const uint32_t*) b)) ? -1 : 0);
}

// Count unique entries in arr. Sorts in place using arr if sort_buf is NULL; arr is untouched if sort_buf is not NULL
static uint32_t count_unique_u32(uint32_t* arr, uint32_t n, uint32_t* sort_buf) {
  // the alternative to using a sorted buffer is an O(n^2) approach
  assert(arr != NULL);
  assert(arr != sort_buf);
  assert(n > 0);
  uint32_t unique = 1;
  uint32_t i;
  if (sort_buf == NULL) {
    sort_buf = arr;
  } else {
    memcpy(sort_buf, arr, n * sizeof(uint32_t));
  }
  qsort(sort_buf, n, sizeof(uint32_t), cmp_u32);
  for (i = 1; i < n; i++) {
    if (sort_buf[i] != sort_buf[i - 1]) {
      unique++;
    }
  }
  return unique;
}

// Get the minimum value in arr where value > gt. Returns UINT32_MAX if none found.
// If 0 is allowed to be the smallest value, set gt = UINT32_MAX
static uint32_t min_gt_u32(uint32_t* arr, uint32_t n, uint32_t gt) {
  uint32_t min = UINT32_MAX;
  uint32_t i;
  for (i = 0; i < n; i++) {
    if (arr[i] < min && (gt == UINT32_MAX || arr[i] > gt)) {
      min = arr[i];
    }
  }
  return min;
}

// Normalize an array in place s.t. final values are in range [0, nidx) but order is retained.
// nidx MUST be the total number of unique entries in arr (see count_unique_u32(...)).
// e.g., [1, 4, 3, 4, 1, 9] (nidx = 4) becomes [0, 2, 1, 2, 0, 3]
static void normalize_to_indexes(uint32_t* arr, uint32_t narr, uint32_t nidx) {
  uint32_t last_min = UINT32_MAX;
  uint32_t i, j;
  for (i = 0; i < nidx; i++) {
    last_min = min_gt_u32(arr, narr, last_min);
    for (j = 0; j < narr; j++) {
      if (arr[j] == last_min) {
        arr[j] = i;
      }
    }
  }
}

// Note: doesn't close previously opened file descriptors if one fails to open
static int open_msrs(int* fds, uint32_t nsockets, uint32_t* cpu_to_socket, uint32_t ncpus) {
  // fds must be all 0s to begin
  (void) nsockets;
  assert(fds != NULL);
  assert(nsockets > 0);
  assert(cpu_to_socket != NULL);
  assert(ncpus > 0);
  uint32_t i;
  const char* env_ro = getenv(ENV_RAPLCAP_READ_ONLY);
  int ro = env_ro == NULL ? 0 : atoi(env_ro);
  for (i = 0; i < ncpus; i++) {
    // must have translated from physical socket value to index (see normalize_to_indexes(...))
    assert(cpu_to_socket[i] < nsockets);
    if (fds[cpu_to_socket[i]] > 0) {
      // already opened msr for this socket
      continue;
    }
    raplcap_log(DEBUG, "open_msrs: Found mapping: cpu=%"PRIu32", socket_idx=%"PRIu32"\n", i, cpu_to_socket[i]);
    // open the cpu MSR for this socket
    if ((fds[cpu_to_socket[i]] = open_msr(i, ro == 0 ? O_RDWR : O_RDONLY)) < 0) {
      return -1;
    }
  }
  return 0;
}

int raplcap_init(raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  uint32_t* cpu_to_socket = NULL;
  raplcap_msr* state = NULL;
  uint64_t msrval;
  uint32_t ncpus;
  uint32_t cpu_model;
  int err_save;
  // check that we recognize the CPU
  if ((cpu_model = msr_get_supported_cpu_model()) == 0) {
    errno = ENOTSUP;
    return -1;
  }
  // need to map CPU IDs to sockets
  if ((ncpus = get_cpu_count()) == 0) {
    raplcap_perror(ERROR, "raplcap_init: get_cpu_count");
    return -1;
  }
  // second half of the buffer is for duplicating/sorting socket mappings in count_unique_u32(...)
  if ((cpu_to_socket = malloc(2 * ncpus * sizeof(uint32_t))) == NULL ||
      (state = malloc(sizeof(raplcap_msr))) == NULL) {
    raplcap_perror(ERROR, "raplcap_init: malloc");
    goto init_fail;
  }
  if (get_cpu_to_socket_mapping(cpu_to_socket, ncpus)) {
    goto init_fail;
  }
  rc->nsockets = count_unique_u32(cpu_to_socket, ncpus, (cpu_to_socket + ncpus));
  raplcap_log(DEBUG, "raplcap_init: ncpus=%"PRIu32", sockets=%"PRIu32"\n", ncpus, rc->nsockets);
  if ((state->fds = calloc(rc->nsockets, sizeof(int))) == NULL) {
    raplcap_perror(ERROR, "raplcap_init: calloc");
    goto init_fail;
  }
  // map cpu IDs to socket indexes; physical socket IDs may not be in range [0, nsockets), so we enforce this
  normalize_to_indexes(cpu_to_socket, ncpus, rc->nsockets);
  raplcap_log(DEBUG, "raplcap_init: normalized physical socket IDs to indexes, opening MSRs...\n");
  rc->state = state;
  if (open_msrs(state->fds, rc->nsockets, cpu_to_socket, ncpus) ||
      read_msr_by_offset(state->fds[0], MSR_RAPL_POWER_UNIT, &msrval, 0)) {
    err_save = errno;
    raplcap_destroy(rc);
    free(cpu_to_socket);
    errno = err_save;
    return -1;
  }
  free(cpu_to_socket);
  // now populate context with unit conversions and function pointers
  msr_get_context(&state->ctx, cpu_model, msrval);
  raplcap_log(DEBUG, "raplcap_init: Initialized\n");
  return 0;

init_fail:
  free(state);
  free(cpu_to_socket);
  rc->nsockets = 0;
  return -1;
}

int raplcap_destroy(raplcap* rc) {
  raplcap_msr* state;
  int err_save = 0;
  uint32_t i;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if ((state = (raplcap_msr*) rc->state) != NULL) {
    for (i = 0; state->fds != NULL && i < rc->nsockets; i++) {
      raplcap_log(DEBUG, "raplcap_destroy: socket=%"PRIu32", fd=%d\n", i, state->fds[i]);
      if (state->fds[i] > 0 && close(state->fds[i])) {
        err_save = errno;
        raplcap_perror(ERROR, "raplcap_destroy: close");
      }
    }
    free(state->fds);
    free(state);
    rc->state = NULL;
  }
  rc->nsockets = 0;
  raplcap_log(DEBUG, "raplcap_destroy: Destroyed\n");
  errno = err_save;
  return err_save ? -1 : 0;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  uint32_t* cpu_to_socket;
  uint32_t ncpus;
  uint32_t nsockets = 0;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->nsockets > 0) {
    return rc->nsockets;
  }
  if ((ncpus = get_cpu_count()) == 0) {
    raplcap_perror(ERROR, "raplcap_get_num_sockets: get_cpu_count");
    return 0;
  }
  if ((cpu_to_socket = malloc(ncpus * sizeof(uint32_t))) == NULL) {
    raplcap_perror(ERROR, "raplcap_get_num_sockets: malloc");
    return 0;
  }
  if (!get_cpu_to_socket_mapping(cpu_to_socket, ncpus)) {
    nsockets = count_unique_u32(cpu_to_socket, ncpus, NULL);
    raplcap_log(DEBUG, "raplcap_get_num_sockets: ncpus=%"PRIu32", nsockets=%"PRIu32"\n", ncpus, nsockets);
  }
  free(cpu_to_socket);
  return nsockets;
}

static raplcap_msr* get_state(uint32_t socket, const raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->nsockets == 0 || rc->state == NULL) {
    // unfortunately can't detect if the context just contains garbage
    raplcap_log(ERROR, "get_state: Context is not initialized\n");
    errno = EINVAL;
    return NULL;
  }
  if (socket >= rc->nsockets) {
    raplcap_log(ERROR, "get_state: Socket %"PRIu32" not in range [0, %"PRIu32")\n", socket, rc->nsockets);
    errno = EINVAL;
    return NULL;
  }
  return (raplcap_msr*) rc->state;
}

int raplcap_is_zone_enabled(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  uint64_t msrval;
  int en[2] = { 1, 1 };
  int ret;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_is_zone_enabled: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  msr_is_zone_enabled(&state->ctx, zone, msrval, &en[0], &en[1]);
  ret = en[0] && en[1];
  if (ret && !raplcap_msr_is_zone_clamping(rc, socket, zone)) {
    raplcap_log(INFO, "Zone is enabled but clamping is not\n");
  }
  return ret;
}

int raplcap_is_zone_supported(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  int ret;
  raplcap_log(DEBUG, "raplcap_is_zone_supported: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0) {
    return -1;
  }
  ret = read_msr_by_offset(state->fds[socket], msr, &msrval, 1) ? 0 : 1;
  raplcap_log(DEBUG, "raplcap_is_zone_supported: socket=%"PRIu32", zone=%d, supported=%d\n", socket, zone, ret);
  return ret;
}

// Enables or disables both the "enable" and "clamping" bits for all constraints
int raplcap_set_zone_enabled(const raplcap* rc, uint32_t socket, raplcap_zone zone, int enabled) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  int ret;
  raplcap_log(DEBUG, "raplcap_set_zone_enabled: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  msrval = msr_set_zone_enabled(&state->ctx, zone, msrval, &enabled, &enabled);
  if ((ret = write_msr_by_offset(state->fds[socket], msr, msrval, 0)) == 0) {
    // try to enable clamping (not supported by all zones or all CPUs)
    msrval = msr_set_zone_clamping(&state->ctx, zone, msrval, &enabled, &enabled);
    if (write_msr_by_offset(state->fds[socket], msr, msrval, 1)) {
      raplcap_log(INFO, "Clamping not available for this zone or platform\n");
    }
  }
  return ret;
}

int raplcap_get_limits(const raplcap* rc, uint32_t socket, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_get_limits: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  msr_get_limits(&state->ctx, zone, msrval, limit_long, limit_short);
  return 0;
}

int raplcap_set_limits(const raplcap* rc, uint32_t socket, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_set_limits: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  msrval = msr_set_limits(&state->ctx, zone, msrval, limit_long, limit_short);
  return write_msr_by_offset(state->fds[socket], msr, msrval, 0);
}

double raplcap_get_energy_counter(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_get_energy_counter: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  return msr_get_energy_counter(&state->ctx, msrval, zone);
}

double raplcap_get_energy_counter_max(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_get_energy_counter_max: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0) {
    return -1;
  }
  return msr_get_energy_counter_max(&state->ctx, zone);
}

int raplcap_msr_is_zone_locked(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_msr_is_zone_locked: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  return msr_is_zone_locked(&state->ctx, zone, msrval);
}

int raplcap_msr_is_zone_clamping(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  uint64_t msrval;
  int clamp[2] = { 1, 1 };
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_msr_is_zone_clamping: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  msr_is_zone_clamping(&state->ctx, zone, msrval, &clamp[0], &clamp[1]);
  return clamp[0] && clamp[1];
}

int raplcap_msr_set_zone_clamping(const raplcap* rc, uint32_t socket, raplcap_zone zone, int clamping) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_PL);
  raplcap_log(DEBUG, "raplcap_msr_set_zone_clamping: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  msrval = msr_set_zone_clamping(&state->ctx, zone, msrval, &clamping, &clamping);
  return write_msr_by_offset(state->fds[socket], msr, msrval, 0);
}

double raplcap_msr_get_time_units(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_msr_get_time_units: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0) {
    return -1;
  }
  return msr_get_time_units(&state->ctx, zone);
}

double raplcap_msr_get_power_units(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_msr_get_power_units: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0) {
    return -1;
  }
  return msr_get_power_units(&state->ctx, zone);
}

double raplcap_msr_get_energy_units(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone, ZONE_OFFSETS_ENERGY);
  raplcap_log(DEBUG, "raplcap_msr_get_energy_units: socket=%"PRIu32", zone=%d\n", socket, zone);
  if (state == NULL || msr < 0) {
    return -1;
  }
  return msr_get_energy_units(&state->ctx, zone);
}
