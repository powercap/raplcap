/**
 * Implementation that uses MSRs directly.
 *
 * See the Intel 64 and IA-32 Architectures Software Developer's Manual for MSR
 * register bit fields.
 *
 * @author Connor Imes
 * @date 2016-10-19
 */
// for popen, pread, pwrite
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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
#define RAPLCAP_IMPL "raplcap-msr"
#include "raplcap-common.h"

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
  // assuming consistent unit values between sockets
  double power_units;
  double time_units;
} raplcap_msr;

static raplcap rc_default;

static int open_msr(uint32_t core) {
  char msr_filename[32];
  // first try using the msr_safe kernel module
  snprintf(msr_filename, sizeof(msr_filename), "/dev/cpu/%"PRIu32"/msr_safe", core);
  int fd = open(msr_filename, O_RDWR);
  if (fd < 0) {
    raplcap_perror(DEBUG, msr_filename);
    raplcap_log(INFO, "msr-safe not available, falling back on standard msr\n");
    // fall back on the standard msr kernel module
    snprintf(msr_filename, sizeof(msr_filename), "/dev/cpu/%"PRIu32"/msr", core);
    fd = open(msr_filename, O_RDWR);
    if (fd < 0) {
      raplcap_perror(ERROR, msr_filename);
      if (errno == ENOENT) {
        raplcap_log(WARN, "Is the msr kernel module loaded?\n");
      }
    }
  }
  return fd;
}

static int read_msr_by_offset(int fd, off_t msr, uint64_t* data) {
  assert(msr >= 0);
  assert(data != NULL);
  if (pread(fd, data, sizeof(uint64_t), msr) == sizeof(uint64_t)) {
    raplcap_log(DEBUG, "read_msr_by_offset: msr=0x%lX, data=0x%016lX\n", msr, *data);
    return 0;
  }
  raplcap_log(ERROR, "read_msr_by_offset(0x%lX): pread: %s\n", msr, strerror(errno));
  return -1;
}

static int write_msr_by_offset(int fd, off_t msr, uint64_t data) {
  assert(msr >= 0);
  raplcap_log(DEBUG, "write_msr_by_offset: msr=0x%lX, data=0x%016lX\n", msr, data);
  if (pwrite(fd, &data, sizeof(uint64_t), msr) == sizeof(uint64_t)) {
    return 0;
  }
  raplcap_log(ERROR, "write_msr_by_offset(0x%lX): pwrite: %s\n", msr, strerror(errno));
  return -1;
}

static off_t zone_to_msr_offset(raplcap_zone zone) {
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      return MSR_PKG_POWER_LIMIT;
    case RAPLCAP_ZONE_CORE:
      return MSR_PP0_POWER_LIMIT;
    case RAPLCAP_ZONE_UNCORE:
      return MSR_PP1_POWER_LIMIT;
    case RAPLCAP_ZONE_DRAM:
      return MSR_DRAM_POWER_LIMIT;
    case RAPLCAP_ZONE_PSYS:
      return MSR_PLATFORM_POWER_LIMIT;
    default:
      raplcap_log(ERROR, "zone_to_msr_offset: Unknown zone: %d\n", zone);
      errno = EINVAL;
      return -1;
  }
}

static uint32_t count_sockets() {
  uint32_t sockets = 0;
  int err_save;
  char output[32];
  // pretty hacky, but seems to work
  FILE* fp = popen("egrep 'physical id' /proc/cpuinfo | sort -u | cut -d : -f 2 | awk '{print $1}' | tail -n 1", "r");
  if (fp == NULL) {
    raplcap_perror(ERROR, "count_sockets: popen");
  } else {
    while (fgets(output, sizeof(output), fp) != NULL) {
      errno = 0;
      sockets = strtoul(output, NULL, 0) + 1;
      if (errno) {
        raplcap_perror(ERROR, "count_sockets: strtoul");
        sockets = 0;
        break;
      }
    }
    // preserve any error
    err_save = errno;
    if (pclose(fp)) {
      raplcap_perror(WARN, "count_sockets: pclose");
    }
    errno = err_save;
  }
  raplcap_log(DEBUG, "count_sockets: sockets=%"PRIu32"\n", sockets);
  return sockets;
}

// Note: doesn't close previously opened file descriptors if one fails to open
static int raplcap_open_msrs(uint32_t sockets, int* fds) {
  // need to find a core for each socket
  assert(sockets > 0);
  assert(fds != NULL);
  uint32_t coreids[sockets];
  int coreids_found[sockets];
  char output[32];
  uint32_t socket;
  uint32_t core;
  uint32_t i;
  // this is REALLY hacky... I'm so sorry. --CKI, 10/22/16
  FILE* fp = popen("egrep 'processor|core id|physical id' /proc/cpuinfo | cut -d : -f 2 | paste - - -  | awk '{print $2 \" \" $1}'", "r");
  if (fp == NULL) {
    raplcap_perror(ERROR, "raplcap_open_msrs: popen");
    return -1;
  }
  memset(coreids, 0, sockets * sizeof(uint32_t));
  memset(coreids_found, 0, sockets * sizeof(int));
  // first column of the output is the socket, second column is the core id
  while (fgets(output, sizeof(output), fp) != NULL) {
    if (sscanf(output, "%"PRIu32" %"PRIu32, &socket, &core) != 2) {
      raplcap_log(ERROR, "raplcap_open_msrs: Failed to parse socket to MSR mapping\n");
      if (pclose(fp)) {
        raplcap_perror(WARN, "raplcap_open_msrs: pclose");
      }
      errno = ENOENT;
      return -1;
    }
    if (socket >= sockets) {
      raplcap_log(ERROR, "raplcap_open_msrs: Socket %"PRIu32" is outside range [0, %"PRIu32")\n", socket, sockets);
      if (pclose(fp)) {
        raplcap_perror(WARN, "raplcap_open_msrs: pclose");
      }
      errno = EINVAL;
      return -1;
    }
    // keep the smallest core value (ideally maps to the first physical core on the socket)
    if (!coreids_found[socket] || core < coreids[socket]) {
      coreids_found[socket] = 1;
      coreids[socket] = core;
    }
  }
  if (pclose(fp)) {
    raplcap_perror(WARN, "raplcap_open_msrs: pclose");
  }
  // verify that we found a MSR for each socket
  for (i = 0; i < sockets; i++) {
    if (!coreids_found[i]) {
      raplcap_log(ERROR, "raplcap_open_msrs: No MSR found for socket %"PRIu32"\n", i);
      errno = ENOENT;
      return -1;
    }
  }
  // now open the MSR for each core
  for (i = 0; i < sockets; i++) {
    if ((fds[i] = open_msr(coreids[i])) < 0) {
      return -1;
    }
  }
  return 0;
}

static uint64_t pow2_u64(uint64_t y) {
  // 2^y
  return ((uint64_t) 1) << y;
}

int raplcap_init(raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  raplcap_msr* state;
  uint64_t msrval;
  int err_save;
  if ((rc->nsockets = count_sockets()) == 0) {
    return -1;
  }
  if ((state = malloc(sizeof(raplcap_msr))) == NULL) {
    raplcap_perror(ERROR, "raplcap_init: malloc");
    return -1;
  }
  if ((state->fds = calloc(rc->nsockets, sizeof(int))) == NULL) {
    raplcap_perror(ERROR, "raplcap_init: calloc");
    free(state);
    return -1;
  }
  rc->state = state;
  if (raplcap_open_msrs(rc->nsockets, state->fds) ||
      read_msr_by_offset(state->fds[0], MSR_RAPL_POWER_UNIT, &msrval)) {
    err_save = errno;
    raplcap_destroy(rc);
    errno = err_save;
    return -1;
  }
  state->power_units = 1.0 / pow2_u64(msrval & 0xf);
  state->time_units = 1.0 / pow2_u64((msrval >> 16) & 0xf);
  raplcap_log(DEBUG, "raplcap_init: Initialized\n");
  return 0;
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
        raplcap_perror(ERROR, "close");
      }
    }
    free(state->fds);
    free(state);
    rc->state = NULL;
  }
  raplcap_log(DEBUG, "raplcap_init: Destroyed\n");
  errno = err_save;
  return err_save ? -1 : 0;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  return rc->nsockets == 0 ? count_sockets() : rc->nsockets;
}

int raplcap_is_zone_supported(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  int ret = raplcap_is_zone_enabled(socket, rc, zone);
  // I/O error indicates zone is not supported, otherwise it's some other error (e.g. EINVAL)
  if (ret == 0 || (ret < 0 && errno == EIO)) {
    ret = 1;
  }
  raplcap_log(DEBUG, "raplcap_is_zone_supported: socket=%"PRIu32", zone=%d, supported=%d\n", socket, zone, ret);
  return ret;
}

static raplcap_msr* get_state(uint32_t socket, const raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state == NULL || socket >= rc->nsockets) {
    errno = EINVAL;
    return NULL;
  }
  return (raplcap_msr*) rc->state;
}

static int is_short_term_allowed(raplcap_zone zone) {
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
    case RAPLCAP_ZONE_PSYS:
      return 1;
    case RAPLCAP_ZONE_CORE:
    case RAPLCAP_ZONE_UNCORE:
    case RAPLCAP_ZONE_DRAM:
      return 0;
    default:
      assert(0);
      return 0;
  }
}

// Get the bits requested and shift right if needed; first and last are inclusive
static uint64_t get_bits(uint64_t msrval, uint8_t first, uint8_t last) {
  assert(first <= last);
  assert(last < 64);
  return (msrval >> first) & (((uint64_t) 1 << (last - first + 1)) - 1);
}

// Replace the requested msrval bits with data the data in situ; first and last are inclusive
static uint64_t replace_bits(uint64_t msrval, uint64_t data, uint8_t first, uint8_t last) {
  assert(first <= last);
  assert(last < 64);
  const uint64_t mask = (((uint64_t) 1 << (last - first + 1)) - 1) << first;
  return (msrval & ~mask) | ((data << first) & mask);
}

int raplcap_is_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone);
  int ret;
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval)) {
    return -1;
  }
  ret = get_bits(msrval, 15, 16) == 0x3 && (is_short_term_allowed(zone) ? get_bits(msrval, 47, 48) == 0x3 : 1);
  if (!ret && get_bits(msrval, 15, 15) == 0x1 && (is_short_term_allowed(zone) ? get_bits(msrval, 47, 47) == 0x1 : 1)) {
    raplcap_log(WARN, "Zone is enabled but clamping is not - use raplcap_set_zone_enabled(...) to enable clamping\n");
    ret = 1;
  }
  raplcap_log(DEBUG, "raplcap_is_zone_enabled: socket=%"PRIu32", zone=%d, enabled=%d\n", socket, zone, ret);
  return ret;
}

// Enables or disables both the "enable" and "clamping" bits for all constraints
int raplcap_set_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone, int enabled) {
  uint64_t msrval;
  const uint64_t enabled_bits = enabled ? 0x3 : 0x0;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval)) {
    return -1;
  }
  msrval = replace_bits(msrval, enabled_bits, 15, 16);
  if (is_short_term_allowed(zone)) {
    msrval = replace_bits(msrval, enabled_bits, 47, 48);
  }
  raplcap_log(DEBUG, "raplcap_set_zone_enabled: socket=%"PRIu32", zone=%d, enabled=%d\n", socket, zone, enabled);
  return write_msr_by_offset(state->fds[socket], msr, msrval);
}

static void to_raplcap(raplcap_limit* limit, double seconds, double watts) {
  assert(limit != NULL);
  limit->seconds = seconds;
  limit->watts = watts;
}

/**
 * Note: Intel's documentation (Section 14.9.3) specifies different conversions for Package and Power Planes.
 * We use the Package equation for Power Planes as well, which the Linux kernel appears to agree with.
 * Time window (seconds) = 2^Y * (1 + F/4) * Time_Unit
 * See the Linux kernel: drivers/powercap/intel_rapl.c:rapl_compute_time_window_core
 */
static double from_msr_time(uint64_t y, uint64_t f, double time_units) {
  raplcap_log(DEBUG, "from_msr_time: y=0x%02lX, f=0x%lX, time_units=%.12f\n", y, f, time_units);
  return pow2_u64(y) * ((4 + f) / 4.0) * time_units;
}

static uint64_t to_msr_time(double seconds, double time_units) {
  assert(seconds > 0);
  assert(time_units > 0);
  // Seconds cannot be shorter than the smallest time unit - log2 would get a negative value and overflow "y".
  // They also cannot be larger than 2^2^5-1 so that log2 doesn't produce a value that uses more than 5 bits for "y".
  // Clamping prevents values outside the allowable range, but precision can still be lost in the conversion.
  static const double MSR_TIME_MIN = 1.0;
  static const double MSR_TIME_MAX = (double) 0xFFFFFFFF;
  double t = seconds / time_units;
  if (t < MSR_TIME_MIN) {
    raplcap_log(WARN, "Time window too small: %.12f sec, using min: %.12f sec\n", seconds, time_units);
    t = MSR_TIME_MIN;
  } else if (t > MSR_TIME_MAX) {
    // "trying" instead of "using" because precision loss will definitely throw off the final value at this extreme
    raplcap_log(WARN, "Time window too large: %.12f sec, trying max: %.12f sec\n", seconds, MSR_TIME_MAX * time_units);
    t = MSR_TIME_MAX;
  }
  // TODO: use an integer log2 function - faster and avoids need for libm
  // y = log2((4*t)/(4+f)), however we can ignore f since t >= 1 and we're casting to an unsigned integer type
  const uint64_t y = (uint64_t) log2(t);
  const uint64_t f = (uint64_t) (4 * (t - pow2_u64(y)) / pow2_u64(y));
  raplcap_log(DEBUG, "to_msr_time: seconds=%.12f, time_units=%.12f, t=%.12f, y=0x%02lX, f=0x%lX\n",
              seconds, time_units, t, y, f);
  return ((y & 0x1F) | ((f & 0x3) << 5));
}

static uint64_t to_msr_power(double watts, double power_units) {
  assert(watts >= 0);
  assert(power_units > 0);
  // Lower bound is 0, but upper bound is limited by what fits in 15 bits
  static const uint64_t MSR_POWER_MAX = 0x7FFF;
  uint64_t p = (uint64_t) (watts / power_units);
  if (p > MSR_POWER_MAX) {
    raplcap_log(WARN, "Power limit too large: %.12f W, using max: %.12f W\n", watts, MSR_POWER_MAX * power_units);
    p = MSR_POWER_MAX;
  }
  raplcap_log(DEBUG, "to_msr_power: watts=%.12f, power_units=%.12f, p=0x%04lX\n", watts, power_units, p);
  return p;
}

int raplcap_get_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  double watts;
  double seconds;
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval)) {
    return -1;
  }
  // power units specified by the "Power Units" field of MSR_RAPL_POWER_UNIT
  // time units specified by the "Time Units" field of MSR_RAPL_POWER_UNIT, plus some additional translation
  if (limit_long != NULL) {
    // bits 14:0
    watts = state->power_units * get_bits(msrval, 0, 14);
    // Here "Y" is the unsigned integer value represented. by bits 21:17, "F" is an unsigned integer represented by
    // bits 23:22
    seconds = from_msr_time(get_bits(msrval, 17, 21), get_bits(msrval, 22, 23), state->time_units);
    to_raplcap(limit_long, seconds, watts);
    raplcap_log(DEBUG, "raplcap_get_limits: socket=%"PRIu32", zone=%d, long_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, limit_long->seconds, limit_long->watts);
  }
  if (limit_short != NULL && is_short_term_allowed(zone)) {
    // bits 46:32
    watts = state->power_units * get_bits(msrval, 32, 46);
    // Here "Y" is the unsigned integer value represented. by bits 53:49, "F" is an unsigned integer represented by
    // bits 55:54.
    // This field may have a hard-coded value in hardware and ignores values written by software.
    seconds = from_msr_time(get_bits(msrval, 49, 53), get_bits(msrval, 54, 55), state->time_units);
    to_raplcap(limit_short, seconds, watts);
    raplcap_log(DEBUG, "raplcap_get_limits: socket=%"PRIu32", zone=%d, short_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, limit_short->seconds, limit_short->watts);
  }
  return 0;
}

int raplcap_set_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval)) {
    return -1;
  }
  if (limit_long != NULL) {
    raplcap_log(DEBUG, "raplcap_set_limits: socket=%"PRIu32", zone=%d, long_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, limit_long->seconds, limit_long->watts);
    if (limit_long->watts > 0) {
      msrval = replace_bits(msrval, to_msr_power(limit_long->watts, state->power_units), 0, 14);
    }
    if (limit_long->seconds > 0) {
      msrval = replace_bits(msrval, to_msr_time(limit_long->seconds, state->time_units), 17, 23);
    }
  }
  if (limit_short != NULL && is_short_term_allowed(zone)) {
    raplcap_log(DEBUG, "raplcap_set_limits: socket=%"PRIu32", zone=%d, short_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, limit_short->seconds, limit_short->watts);
    if (limit_short->watts > 0) {
      msrval = replace_bits(msrval, to_msr_power(limit_short->watts, state->power_units), 32, 46);
    }
    if (limit_short->seconds > 0) {
      msrval = replace_bits(msrval, to_msr_time(limit_short->seconds, state->time_units), 49, 55);
    }
  }
  return write_msr_by_offset(state->fds[socket], msr, msrval);
}
