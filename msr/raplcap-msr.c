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
    // fall back on the standard msr kernel module
    snprintf(msr_filename, sizeof(msr_filename), "/dev/cpu/%"PRIu32"/msr", core);
    fd = open(msr_filename, O_RDWR);
  }
  return fd;
}

static int read_msr_by_offset(int fd, off_t msr, uint64_t* data) {
  assert(msr >= 0);
  assert(data != NULL);
  return pread(fd, data, sizeof(uint64_t), msr) == sizeof(uint64_t) ? 0 : -1;
}

static int write_msr_by_offset(int fd, off_t msr, uint64_t data) {
  assert(msr >= 0);
  return pwrite(fd, &data, sizeof(uint64_t), msr) == sizeof(uint64_t) ? 0 : -1;
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
  if (fp != NULL) {
    while (fgets(output, sizeof(output), fp) != NULL) {
      errno = 0;
      sockets = strtoul(output, NULL, 0) + 1;
      if (errno) {
        sockets = 0;
        break;
      }
    }
    // preserve any error
    err_save = errno;
    pclose(fp);
    errno = err_save;
  }
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
    return 0;
  }
  memset(coreids, 0, sockets * sizeof(uint32_t));
  memset(coreids_found, 0, sockets * sizeof(int));
  // first column of the output is the socket, second column is the core id
  while (fgets(output, sizeof(output), fp) != NULL) {
    if (sscanf(output, "%"PRIu32" %"PRIu32, &socket, &core) != 2) {
      fprintf(stderr, "raplcap_open_msrs: Failed to parse socket to MSR mapping\n");
      pclose(fp);
      errno = ENOENT;
      return -1;
    }
    if (socket >= sockets) {
      fprintf(stderr, "raplcap_open_msrs: Found more sockets than expected: %"PRIu32" instead of %"PRIu32"\n", socket + 1, sockets);
      pclose(fp);
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
  uint64_t msrval;
  int err_save;
  rc->nsockets = raplcap_get_num_sockets(NULL);
  raplcap_msr* state = malloc(sizeof(raplcap_msr));
  if (rc->nsockets == 0 || state == NULL) {
    free(state);
    return -1;
  }
  rc->state = state;
  state->fds = calloc(rc->nsockets, sizeof(int));
  if (state->fds == NULL ||
      raplcap_open_msrs(rc->nsockets, state->fds) ||
      read_msr_by_offset(state->fds[0], MSR_RAPL_POWER_UNIT, &msrval)) {
    err_save = errno;
    raplcap_destroy(rc);
    errno = err_save;
    return -1;
  }
  state->power_units = 1.0 / pow2_u64(msrval & 0xf);
  state->time_units = 1.0 / pow2_u64((msrval >> 16) & 0xf);
  return 0;
}

int raplcap_destroy(raplcap* rc) {
  int err_save = 0;
  uint32_t i;
  if (rc == NULL) {
    rc = &rc_default;
  }
  if (rc->state != NULL) {
    raplcap_msr* state = (raplcap_msr*) rc->state;
    for (i = 0; state->fds != NULL && i < rc->nsockets; i++) {
      if (state->fds[i] > 0 && close(state->fds[i])) {
        err_save = errno;
      }
    }
    free(state->fds);
    free(state);
    rc->state = NULL;
    errno = err_save;
  }
  return err_save ? -1 : 0;
}

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  if (rc == NULL) {
    rc = &rc_default;
  }
  return rc->nsockets == 0 ? count_sockets() : rc->nsockets;
}

int raplcap_is_zone_supported(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  if (raplcap_is_zone_enabled(socket, rc, zone) < 0) {
    // I/O error indicates zone is not supported, otherwise it's some other error (e.g. EINVAL)
    return errno == EIO ? 0 : -1;
  }
  return 1;
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

// Zone is considered enabled only if both "enable" and "clamping" bits are set for all constraints
int raplcap_is_zone_enabled(uint32_t socket, const raplcap* rc, raplcap_zone zone) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval)) {
    return -1;
  }
  return get_bits(msrval, 15, 16) == 0x3 && (is_short_term_allowed(zone) ? get_bits(msrval, 47, 48) == 0x3 : 1);
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
  return pow2_u64(y) * ((4 + f) / 4.0) * time_units;
}

static double clamp_f(double min, double max, double val) {
  return val < min ? min : val > max ? max : val;
}

static uint64_t to_msr_time(double seconds, double time_units) {
  assert(seconds > 0);
  assert(time_units > 0);
  // Seconds cannot be shorter than the smallest time unit - log2 would get a negative value and overflow "y".
  // They also cannot be larger than 2^2^5-1 so that log2 doesn't produce a value that uses more than 5 bits for "y".
  // Clamping just prevents values outside the allowable range, but precision can still be lost in the conversion.
  const double d = clamp_f(1.0, (double) ((uint32_t) 0xFFFFFFFF), seconds / time_units);
  // TODO: use an integer log2 function - faster and avoids need for libm
  // y = log2((4*d)/(4+f)), however we can ignore f since d >= 1 and we're casting to an unsigned integer type
  const uint64_t y = (uint64_t) log2(d);
  const uint64_t f = (uint64_t) (4 * (d - pow2_u64(y)) / pow2_u64(y));
  return ((y & 0x1F) | ((f & 0x3) << 5));
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
  }
  if (limit_short != NULL && is_short_term_allowed(zone)) {
    // bits 46:32
    watts = state->power_units * get_bits(msrval, 32, 46);
    // Here "Y" is the unsigned integer value represented. by bits 53:49, "F" is an unsigned integer represented by
    // bits 55:54.
    // This field may have a hard-coded value in hardware and ignores values written by software.
    seconds = from_msr_time(get_bits(msrval, 49, 53), get_bits(msrval, 54, 55), state->time_units);
    to_raplcap(limit_short, seconds, watts);
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
    if (limit_long->watts > 0) {
      msrval = replace_bits(msrval, (uint64_t) (limit_long->watts / state->power_units), 0, 14);
    }
    if (limit_long->seconds > 0) {
      msrval = replace_bits(msrval, to_msr_time(limit_long->seconds, state->time_units), 17, 23);
    }
  }
  if (limit_short != NULL && is_short_term_allowed(zone)) {
    if (limit_short->watts > 0) {
      msrval = replace_bits(msrval, (uint64_t) (limit_short->watts / state->power_units), 32, 46);
    }
    if (limit_short->seconds > 0) {
      msrval = replace_bits(msrval, to_msr_time(limit_short->seconds, state->time_units), 49, 55);
    }
  }
  return write_msr_by_offset(state->fds[socket], msr, msrval);
}
