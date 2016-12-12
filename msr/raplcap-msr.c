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
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include "raplcap.h"

#define MSR_RAPL_POWER_UNIT       0x606

/* Package RAPL Domain */
#define MSR_PKG_POWER_LIMIT       0x610

/* PP0 RAPL Domain */
#define MSR_PP0_POWER_LIMIT       0x638

/* PP1 RAPL Domain, may reflect to uncore devices */
#define MSR_PP1_POWER_LIMIT       0x640

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT      0x618

/* Platform (PSys) Domain (Skylake and newer) */
#define MSR_PLATFORM_POWER_LIMIT  0x65C

typedef struct raplcap_msr {
  int* fds;
  uint32_t nfds;
  // assuming consistent unit values between sockets
  double power_units;
  double time_units;
} raplcap_msr;

static inline int open_msr(uint32_t core) {
  char msr_filename[32];
  int fd;
  // first try using the msr_safe kernel module
  snprintf(msr_filename, sizeof(msr_filename), "/dev/cpu/%"PRIu32"/msr_safe", core);
  fd = open(msr_filename, O_RDWR);
  if (fd < 0) {
    // fall back on the standard msr kernel module
    snprintf(msr_filename, sizeof(msr_filename), "/dev/cpu/%"PRIu32"/msr", core);
    fd = open(msr_filename, O_RDWR);
  }
  return fd < 0 ? -1 : fd;
}

static inline int read_msr_by_offset(int fd, off_t msr, uint64_t* data) {
  assert(data != NULL);
  return pread(fd, data, sizeof(uint64_t), msr) == sizeof(uint64_t) ? 0 : -1;
}

static inline int read_msr(int fd, raplcap_zone z, uint64_t* data) {
  off_t msr;
  switch (z) {
    case RAPLCAP_ZONE_PACKAGE:
      msr = MSR_PKG_POWER_LIMIT;
      break;
    case RAPLCAP_ZONE_CORE:
      msr = MSR_PP0_POWER_LIMIT;
      break;
    case RAPLCAP_ZONE_UNCORE:
      msr = MSR_PP1_POWER_LIMIT;
      break;
    case RAPLCAP_ZONE_DRAM:
      msr = MSR_DRAM_POWER_LIMIT;
      break;
    case RAPLCAP_ZONE_PSYS:
      msr = MSR_PLATFORM_POWER_LIMIT;
      break;
    default:
      errno = EINVAL;
      return -1;
  }
  return read_msr_by_offset(fd, msr, data);
}

static inline int write_msr(int fd, raplcap_zone z, uint64_t data) {
  off_t msr;
  switch (z) {
    case RAPLCAP_ZONE_PACKAGE:
      msr = MSR_PKG_POWER_LIMIT;
      break;
    case RAPLCAP_ZONE_CORE:
      msr = MSR_PP0_POWER_LIMIT;
      break;
    case RAPLCAP_ZONE_UNCORE:
      msr = MSR_PP1_POWER_LIMIT;
      break;
    case RAPLCAP_ZONE_DRAM:
      msr = MSR_DRAM_POWER_LIMIT;
      break;
    case RAPLCAP_ZONE_PSYS:
      msr = MSR_PLATFORM_POWER_LIMIT;
      break;
    default:
      errno = EINVAL;
      return -1;
  }
  return pwrite(fd, &data, sizeof(uint64_t), msr) == sizeof(uint64_t) ? 0 : -1;
}

static inline uint32_t count_sockets() {
  // pretty hacky, but seems to work
  const char* cmd = "egrep 'physical id' /proc/cpuinfo | sort -u | cut -d : -f 2 | awk '{print $1}' | tail -n 1";
  uint32_t sockets = 0;
  int err_save = 0;
  char output[32] = {'\0'};
  FILE* fp = popen(cmd, "r");
  if (fp == NULL) {
    return 0;
  }
  while (fgets(output, sizeof(output) - 1, fp) != NULL) {
    errno = 0;
    sockets = strtoul(output, NULL, 0);
    if (sockets == 0 && errno) {
      // preserve the error
      err_save = errno;
      break;
    }
    sockets++;
  }
  pclose(fp);
  errno = err_save;
  return sockets;
}

static inline int raplcap_open_msrs(uint32_t sockets, int* fds) {
  // need to find a core for each socket
  assert(sockets > 0);
  assert(fds != NULL);
  uint32_t coreids[sockets];
  int coreids_found[sockets];
  char output[32] = {'\0'};
  uint32_t socket;
  uint32_t core;
  uint32_t i, j;
  int err_save;
  // this is REALLY hacky... I'm so sorry. --CKI, 10/22/16
  const char* cmd = "egrep 'processor|core id|physical id' /proc/cpuinfo | cut -d : -f 2 | paste - - -  | awk '{print $2 \" \" $1}'";
  FILE* fp = popen(cmd, "r");
  if (fp == NULL) {
    return 0;
  }
  memset(coreids, 0, sockets * sizeof(uint32_t));
  memset(coreids_found, 0, sockets * sizeof(int));
  // first column of the output is the socket, second column is the core id
  while (fgets(output, sizeof(output) - 1, fp) != NULL) {
    if (sscanf(output, "%"PRIu32" %"PRIu32, &socket, &core) != 2) {
      fprintf(stderr, "raplcap_open_msrs: Failed to parse socket to MSR mapping\n");
      errno = ENOENT;
      return -1;
    }
    if (socket >= sockets) {
      fprintf(stderr, "raplcap_open_msrs: Found more sockets than expected: %"PRIu32" instead of %"PRIu32"\n", socket + 1, sockets);
      errno = EINVAL;
      return -1;
    }
    // keep the smallest core value (ideally maps to the first physical core on the socket)
    if (!coreids_found[socket] || core < coreids[socket]) {
      coreids_found[socket] = 1;
      coreids[socket] = core;
    }
  }
  pclose(fp);
  // verify that we found a MSR for each socket
  for (i = 0; i < sockets; i++) {
    if (!coreids_found[i]) {
      fprintf(stderr, "raplcap_open_msrs: Failed to find a MSR for socket %"PRIu32"\n", i);
      errno = ENOENT;
      return -1;
    }
  }
  // now open the MSR for each core
  for (i = 0; i < sockets; i++) {
    fds[i] = open_msr(coreids[i]);
    if (fds[i] < 0) {
      err_save = errno;
      // cleanup
      for (j = 0; j < i; j++) {
        close(fds[j]);
      }
      errno = err_save;
      return -1;
    }
  }
  return 0;
}

int raplcap_init(raplcap* rc) {
  if (rc == NULL) {
    errno = EINVAL;
    return -1;
  }
  memset(rc, 0, sizeof(raplcap));
  uint32_t sockets = raplcap_get_num_sockets(NULL);
  if (sockets == 0) {
    return -1;
  }
  raplcap_msr* state = malloc(sizeof(raplcap_msr));
  if (state == NULL) {
    return -1;
  }
  state->fds = calloc(sockets, sizeof(int));
  if (state->fds == NULL) {
    free(state);
    return -1;
  }
  if (raplcap_open_msrs(sockets, state->fds)) {
    free(state->fds);
    free(state);
    return -1;
  }

  rc->nsockets = sockets;
  rc->state = state;

  uint64_t msrval;
  if (read_msr_by_offset(state->fds[0], MSR_RAPL_POWER_UNIT, &msrval)) {
    raplcap_destroy(rc);
  }
  state->power_units = pow(0.5, (double) (msrval & 0xf));
  state->time_units = pow(0.5, (double) ((msrval >> 16) & 0xf));

  return 0;
}

int raplcap_destroy(raplcap* rc) {
  int ret = 0;
  int err_save = 0;
  uint32_t i;
  if (rc != NULL && rc->state != NULL) {
    raplcap_msr* state = (raplcap_msr*) rc->state;
    for (i = 0; i < state->nfds; i++) {
      if (state->fds[i] > 0 && close(state->fds[i])) {
        err_save = errno;
        ret = -1;
      }
    }
    free(state->fds);
    free(state);
    rc->state = NULL;
  }
  errno = err_save;
  return ret;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  return rc == NULL ? count_sockets() : rc->nsockets;
}

int raplcap_is_zone_supported(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  if (rc == NULL || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  // TODO: Discover dynamically
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
    case RAPLCAP_ZONE_CORE:
      // always supported
      return 1;
    case RAPLCAP_ZONE_UNCORE:
#if defined(RAPL_UNCORE_SUPPORTED)
      return RAPL_UNCORE_SUPPORTED;
#else
      return 1;
#endif
    case RAPLCAP_ZONE_DRAM:
#if defined(RAPL_DRAM_SUPPORTED)
      return RAPL_DRAM_SUPPORTED;
#else
      return 1;
#endif
    case RAPLCAP_ZONE_PSYS:
#if defined(RAPL_PSYS_SUPPORTED)
      return RAPL_PSYS_SUPPORTED;
#else
      return 1;
#endif
    default:
      errno = EINVAL;
      return -1;
  }
}

/**
 * Get the bits requested and shift right if needed.
 * First and last are inclusive.
 */
static inline uint64_t get_bits(uint64_t msrval, uint8_t first, uint8_t last) {
  assert(first <= last);
  assert(last < 64);
  return (msrval >> first) & ((1 << (last - first + 1)) - 1);
}

static inline int is_pkg_platform_enabled(uint64_t msrval) {
  // since setting enables both, only return true if both are enabled`
  return get_bits(msrval, 15, 15) && get_bits(msrval, 47, 47);
}

static inline int is_core_uncore_dram_enabled(uint64_t msrval) {
  return get_bits(msrval, 15, 15);
}

int raplcap_is_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  int ret;
  uint64_t msrval = 0;
  if (rc == NULL || rc->state == NULL || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  const raplcap_msr* state = (raplcap_msr*) rc->state;
  if (read_msr(state->fds[socket], zone, &msrval)) {
    return -1;
  }
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
    case RAPLCAP_ZONE_PSYS:
      ret = is_pkg_platform_enabled(msrval);
      break;
    case RAPLCAP_ZONE_CORE:
    case RAPLCAP_ZONE_UNCORE:
    case RAPLCAP_ZONE_DRAM:
      ret = is_core_uncore_dram_enabled(msrval);
      break;
    default:
      errno = EINVAL;
      ret = -1;
      break;
  }
  return ret;
}

static inline uint64_t replace_bits(uint64_t msrval, uint64_t data, uint8_t first, uint8_t last) {
  // first and last are inclusive
  assert(first <= last);
  assert(last < 64);
  uint64_t mask = (((uint64_t) 1 << (last - first + 1)) - 1) << first;
  data = data << first;
  return (msrval & ~mask) | (data & mask);
}

static inline uint64_t set_pkg_platform_enabled(uint64_t msrval, int enabled) {
  const uint64_t set = enabled ? 1 : 0;
  // set RAPL enable
  msrval = replace_bits(msrval, set, 15, 15);
  msrval = replace_bits(msrval, set, 47, 47);
  // set clamping enable
  msrval = replace_bits(msrval, set, 16, 16);
  return replace_bits(msrval, set, 48, 48);
}

static inline uint64_t set_core_uncore_dram_enabled(uint64_t msrval, int enabled) {
  const uint64_t set = enabled ? 1 : 0;
  // set RAPL enable
  msrval = replace_bits(msrval, set, 15, 15);
  // set clamping enable
  return replace_bits(msrval, set, 16, 16);
}

int raplcap_set_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone, int enabled) {
  uint64_t msrval = 0;
  if (rc == NULL || rc->state == NULL || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  const raplcap_msr* state = (raplcap_msr*) rc->state;
  if (read_msr(state->fds[socket], zone, &msrval)) {
    return -1;
  }
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
    case RAPLCAP_ZONE_PSYS:
      msrval = set_pkg_platform_enabled(msrval, enabled);
      break;
    case RAPLCAP_ZONE_CORE:
    case RAPLCAP_ZONE_UNCORE:
    case RAPLCAP_ZONE_DRAM:
      msrval = set_core_uncore_dram_enabled(msrval, enabled);
      break;
    default:
      errno = EINVAL;
      return -1;
  }
  return write_msr(state->fds[socket], zone, msrval);
}

static inline void to_raplcap(raplcap_limit* limit, double seconds, double watts) {
  assert(limit != NULL);
  limit->seconds = seconds;
  limit->watts = watts;
}

/**
 * F is a single-digit decimal floating-point value between 1.0 and 1.3 with
 * the fraction digit represented by 2 bits.
 */
static inline double to_time_window_F(uint64_t bits) {
  assert(bits <= 3);
  return 1.0 + 0.1 * bits;
}

/**
 * Get the power and time window values for long and short limits.
 */
static inline void get_pkg_platform(uint64_t msrval, const raplcap_msr* state,
                                    raplcap_limit* limit_long, raplcap_limit* limit_short) {
  assert(state != NULL);
  double watts;
  double seconds;
  if (limit_long != NULL) {
    // bits 14:0
    // The unit of this field is specified by the “Power Units” field of MSR_RAPL_POWER_UNIT.
    watts = state->power_units * get_bits(msrval, 0, 14);
    // Time limit = 2^Y * (1.0 + Z/4.0) * Time_Unit
    // Here “Y” is the unsigned integer value represented. by bits 21:17, “Z” is an unsigned integer represented by
    // bits 23:22. “Time_Unit” is specified by the “Time Units” field of MSR_RAPL_POWER_UNIT
    seconds = pow(2.0, (double) get_bits(msrval, 17, 21)) * (1.0 + (get_bits(msrval, 22, 23) / 4.0)) * state->time_units;
    to_raplcap(limit_long, seconds, watts);
  }
  if (limit_short != NULL) {
    // bits 46:32
    // The unit of this field is specified by the “Power Units” field of MSR_RAPL_POWER_UNIT.
    watts = state->power_units * get_bits(msrval, 32, 46);
    // Time limit = 2^Y * (1.0 + Z/4.0) * Time_Unit
    // Here “Y” is the unsigned integer value represented. by bits 53:49, “Z” is an unsigned integer represented by
    // bits 55:54. “Time_Unit” is specified by the “Time Units” field of MSR_RAPL_POWER_UNIT. This field may have
    // a hard-coded value in hardware and ignores values written by software.
    seconds =  pow(2.0, (double) get_bits(msrval, 49, 53)) * (1.0 + (get_bits(msrval, 54, 55) / 4.0)) * state->time_units;
    to_raplcap(limit_short, seconds, watts);
  }
}

static inline void get_core_uncore_dram(uint64_t msrval, const raplcap_msr* state,
                                        raplcap_limit* limit_long) {
  assert(state != NULL);
  double watts;
  double seconds;
  if (limit_long != NULL) {
    // bits 14:0
    // units specified by the “Power Units” field of MSR_RAPL_POWER_UNIT
    watts = state->power_units * get_bits(msrval, 0, 14);
    // bits 21:17
    // 2^Y *F; where F is a single-digit decimal floating-point value between 1.0 and 1.3 with the fraction digit
    // represented by bits 23:22, Y is an unsigned integer represented by bits 21:17. The unit of this field is specified
    // by the “Time Units” field of MSR_RAPL_POWER_UNIT.
    seconds = pow(2.0, (double) get_bits(msrval, 17, 21)) * to_time_window_F(get_bits(msrval, 22, 23)) * state->time_units;
    to_raplcap(limit_long, seconds, watts);
  }
}

int raplcap_get_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  int ret = 0;
  uint64_t msrval = 0;
  if (rc == NULL || rc->state == NULL || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  const raplcap_msr* state = (raplcap_msr*) rc->state;
  if (read_msr(state->fds[socket], zone, &msrval)) {
    return -1;
  }
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
    case RAPLCAP_ZONE_PSYS:
      get_pkg_platform(msrval, state, limit_long, limit_short);
      break;
    case RAPLCAP_ZONE_CORE:
    case RAPLCAP_ZONE_UNCORE:
    case RAPLCAP_ZONE_DRAM:
      get_core_uncore_dram(msrval, state, limit_long);
      break;
    default:
      errno = EINVAL;
      ret = -1;
      break;
  }
  return ret;
}

/**
 * Computes bit field based on equations in get_pkg_platform(...).
 * Needs to solve for a different value in the equation though.
 */
static inline uint64_t set_pkg_platform(uint64_t msrval, const raplcap_msr* state,
                                        const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  assert(state != NULL);
  double msr_pwr;
  double msr_time;
  if (limit_long != NULL) {
    if (limit_long->watts > 0) {
      msr_pwr = limit_long->watts / state->power_units;
      msrval = replace_bits(msrval, msr_pwr, 0, 14);
    }
    if (limit_long->seconds > 0) {
      msr_time = log2((4.0 * limit_long->seconds) / (state->time_units * (get_bits(msrval, 22, 23) + 4.0)));
      msrval = replace_bits(msrval, msr_time, 17, 21);
    }
  }
  if (limit_short != NULL) {
    if (limit_short->watts > 0) {
      msr_pwr = limit_short->watts / state->power_units;
      msrval = replace_bits(msrval, msr_pwr, 32, 46);
    }
    if (limit_short->seconds > 0) {
      msr_time = log2((4.0 * limit_short->seconds) / (state->time_units * (get_bits(msrval, 54, 55) + 4.0)));
      msrval = replace_bits(msrval, msr_time, 49, 53);
    }
  }
  return msrval;
}

/**
 * Computes bit field based on equations in get_core_uncore_dram(...)
 * Needs to solve for a different value in the equation though.
 */
static inline uint64_t set_core_uncore_dram(uint64_t msrval, const raplcap_msr* state,
                                            const raplcap_limit* limit_long) {
  assert(state != NULL);
  double msr_pwr;
  double msr_time;
  if (limit_long != NULL) {
    if (limit_long->watts > 0) {
      msr_pwr = limit_long->watts / state->power_units;
      msrval = replace_bits(msrval, msr_pwr, 0, 14);
    }
    if (limit_long->seconds > 0) {
      msr_time = log2(limit_long->seconds / to_time_window_F(get_bits(msrval, 22, 23)));
      msrval = replace_bits(msrval, msr_time, 17, 21);
    }
  }
  return msrval;
}

int raplcap_set_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  uint64_t msrval = 0;
  if (rc == NULL || rc->state == NULL || socket >= rc->nsockets) {
    errno = EINVAL;
    return -1;
  }
  const raplcap_msr* state = (raplcap_msr*) rc->state;
  if (read_msr(state->fds[socket], zone, &msrval)) {
    return -1;
  }
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
    case RAPLCAP_ZONE_PSYS:
      msrval = set_pkg_platform(msrval, state, limit_long, limit_short);
      break;
    case RAPLCAP_ZONE_CORE:
    case RAPLCAP_ZONE_UNCORE:
    case RAPLCAP_ZONE_DRAM:
      msrval = set_core_uncore_dram(msrval, state, limit_long);
      break;
    default:
      errno = EINVAL;
      return -1;
  }
  return write_msr(state->fds[socket], zone, msrval);
}
