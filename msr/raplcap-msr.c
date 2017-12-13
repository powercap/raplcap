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
#define RAPLCAP_IMPL "raplcap-msr"
#include "raplcap-common.h"
#include "raplcap-cpuid.h"

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

#define NZONES (RAPLCAP_ZONE_PSYS + 1)

typedef uint64_t (fn_to_msr) (double value, double units);
typedef double (fn_from_msr) (uint64_t bits, double units);

typedef struct raplcap_msr_zone_cfg {
  fn_to_msr* to_msr_tw;
  fn_from_msr* from_msr_tw;
  fn_to_msr* to_msr_pl;
  fn_from_msr* from_msr_pl;
  uint8_t constraints;
} raplcap_msr_zone_cfg;

typedef struct raplcap_msr {
  int* fds;
  // assuming consistent unit values between sockets
  double power_units;
  double time_units;
  const raplcap_msr_zone_cfg* cfg;
  uint32_t cpu_model;
} raplcap_msr;

static raplcap rc_default;

// 2^y
static uint64_t pow2_u64(uint64_t y) {
  return ((uint64_t) 1) << y;
}

// log2(y); returns 0 for y = 0
static uint64_t log2_u64(uint64_t y) {
  uint8_t ret = 0;
  while (y >>= 1) {
    ret++;
  }
  return ret;
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

// Section 14.9.1
static double from_msr_pu_default(uint64_t msrval) {
  return 1.0 / pow2_u64(msrval & 0xF);
}

// Table 35-8
static double from_msr_pu_atom(uint64_t msrval) {
  return pow2_u64(msrval & 0xF) / 1000.0;
}

// Section 14.9.1
static double from_msr_tu_default(uint64_t msrval) {
  // For Atom, Table 35-8 specifies that field value is always 0x0, meaning 1 second, so this works still
  return 1.0 / pow2_u64((msrval >> 16) & 0xF);
}

// Section 14.9.1
static double from_msr_pl_default(uint64_t bits, double power_units) {
  assert(power_units > 0);
  double watts = power_units * bits;
  raplcap_log(DEBUG, "from_msr_pl_default: bits=%04lX, power_units=%.12f, watts=%.12f\n", bits, power_units, watts);
  return watts;
}

// Section 14.9.1
static uint64_t to_msr_pl_default(double watts, double power_units) {
  assert(watts >= 0);
  assert(power_units > 0);
  // Lower bound is 0, but upper bound is limited by what fits in 15 bits
  static const uint64_t MSR_POWER_MAX = 0x7FFF;
  uint64_t bits = (uint64_t) (watts / power_units);
  if (bits > MSR_POWER_MAX) {
    raplcap_log(WARN, "Power limit too large: %.12f W, using max: %.12f W\n", watts, MSR_POWER_MAX * power_units);
    bits = MSR_POWER_MAX;
  }
  raplcap_log(DEBUG, "to_msr_pl_default: watts=%.12f, power_units=%.12f, bits=0x%04lX\n", watts, power_units, bits);
  return bits;
}

/**
 * Note: Intel's documentation (Section 14.9.3) specifies different conversions for Package and Power Planes.
 * We use the Package equation for Power Planes as well, which the Linux kernel appears to agree with.
 * Time window (seconds) = 2^Y * (1 + F/4) * Time_Unit
 * See the Linux kernel: drivers/powercap/intel_rapl.c:rapl_compute_time_window_core
 */
// Section 14.9.3
static double from_msr_tw_default(uint64_t bits, double time_units) {
  assert(time_units > 0);
  // "Y" is an unsigned integer value represented by lower 5 bits
  // "F" is an unsigned integer value represented by upper 2 bits
  uint64_t y = bits & 0x1F;
  uint64_t f = (bits >> 5) & 0x3;
  double seconds = pow2_u64(y) * ((4 + f) / 4.0) * time_units;
  raplcap_log(DEBUG, "from_msr_tw_default: bits=0x%02lX, time_units=%.12f, y=0x%02lX, f=0x%lX, seconds=%.12f\n",
              bits, time_units, y, f, seconds);
  return seconds;
}

// Section 14.9.3
static uint64_t to_msr_tw_default(double seconds, double time_units) {
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
  // y = log2((4*t)/(4+f)); we can ignore "f" since t >= 1 and 0 <= f <= 3; we can also drop the real part of "t"
  const uint64_t y = log2_u64((uint64_t) t);
  // f = (4*t)/(2^y)-4; the real part of "t" only matters for t < 4, otherwise it's insignificant in computing "f"
  const uint64_t f = (((uint64_t) (4 * t)) / pow2_u64(y)) - 4;
  uint64_t bits = ((y & 0x1F) | ((f & 0x3) << 5));
  raplcap_log(DEBUG, "to_msr_tw_default: seconds=%.12f, time_units=%.12f, t=%.12f, y=0x%02lX, f=0x%lX, bits=0x%02lX\n",
              seconds, time_units, t, y, f, bits);
  return bits;
}

// Table 35-8
static double from_msr_tw_atom(uint64_t bits, double time_units) {
  (void) time_units;
  // If 0 is specified in bits [23:17], defaults to 1 second window.
  double seconds = bits ? bits : 1.0;
  raplcap_log(DEBUG, "from_msr_tw_atom: bits=0x%02lX, seconds=%.12f\n", bits, seconds);
  return seconds;
}

// Table 35-8
static uint64_t to_msr_tw_atom(double seconds, double time_units) {
  (void) time_units;
  assert(seconds > 0);
  // round to nearest second
  uint64_t bits = (uint64_t) (seconds + 0.5);
  raplcap_log(DEBUG, "to_msr_tw_atom: seconds=%.12f, bits=0x%02lX\n", seconds, bits);
  return bits;
}

// Table 35-11
static double from_msr_tw_atom_airmont(uint64_t bits, double time_units) {
  // Used only for Airmont PP0 (CORE) zone
  (void) time_units;
  // If 0 is specified in bits [23:17], defaults to 1 second window.
  double seconds = bits ? bits * 5.0 : 1.0;
  raplcap_log(DEBUG, "from_msr_tw_atom_airmont: bits=0x%02lX, seconds=%.12f\n", bits, seconds);
  return seconds;
}

// Table 35-11
static uint64_t to_msr_tw_atom_airmont(double seconds, double time_units) {
  // Used only for Airmont PP0 (CORE) zone
  (void) time_units;
  assert(seconds > 0);
  static const uint64_t MSR_TIME_WINDOW_MIN = 0x0; // 1 second
  static const uint64_t MSR_TIME_WINDOW_MAX = 0xA; // 50 seconds
  if (seconds < 1) {
    raplcap_log(WARN, "Time window too small: %.12f sec, using min: 1 sec\n", seconds);
    return MSR_TIME_WINDOW_MIN;
  }
  if (seconds > 50) {
    raplcap_log(WARN, "Time window too large: %.12f sec, using max: 50 sec\n", seconds);
    return MSR_TIME_WINDOW_MAX;
  }
  // round to nearest multiple of 5
  uint64_t bits = (uint64_t) ((seconds + 2.5) / 5.0);
  raplcap_log(DEBUG, "to_msr_tw_atom_airmont: seconds=%.12f, time_units=%.12f, bits=0x%02lX\n",
              seconds, time_units, bits);
  return bits;
}

static const raplcap_msr_zone_cfg CFG_DEFAULT[NZONES] = {
  { // PACKAGE
    .to_msr_tw = to_msr_tw_default,
    .from_msr_tw = from_msr_tw_default,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 2
  },
  { // CORE
    .to_msr_tw = to_msr_tw_default,
    .from_msr_tw = from_msr_tw_default,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // UNCORE
    .to_msr_tw = to_msr_tw_default,
    .from_msr_tw = from_msr_tw_default,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // DRAM
    .to_msr_tw = to_msr_tw_default,
    .from_msr_tw = from_msr_tw_default,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // PSYS
    .to_msr_tw = to_msr_tw_default,
    .from_msr_tw = from_msr_tw_default,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 2
  }
};

static const raplcap_msr_zone_cfg CFG_ATOM[NZONES] = {
  { // PACKAGE
    .to_msr_tw = to_msr_tw_atom,
    .from_msr_tw = from_msr_tw_atom,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // CORE
    .to_msr_tw = to_msr_tw_atom,
    .from_msr_tw = from_msr_tw_atom,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // UNCORE
    .to_msr_tw = to_msr_tw_atom,
    .from_msr_tw = from_msr_tw_atom,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // DRAM
    .to_msr_tw = to_msr_tw_atom,
    .from_msr_tw = from_msr_tw_atom,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // PSYS
    .to_msr_tw = to_msr_tw_atom,
    .from_msr_tw = from_msr_tw_atom,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 2
  }
};

// only the CORE time window is different from other ATOM CPUs
static const raplcap_msr_zone_cfg CFG_ATOM_AIRMONT[NZONES] = {
  { // PACKAGE
    .to_msr_tw = to_msr_tw_atom,
    .from_msr_tw = from_msr_tw_atom,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // CORE
    .to_msr_tw = to_msr_tw_atom_airmont,
    .from_msr_tw = from_msr_tw_atom_airmont,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // UNCORE
    .to_msr_tw = to_msr_tw_atom,
    .from_msr_tw = from_msr_tw_atom,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // DRAM
    .to_msr_tw = to_msr_tw_atom,
    .from_msr_tw = from_msr_tw_atom,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 1
  },
  { // PSYS
    .to_msr_tw = to_msr_tw_atom,
    .from_msr_tw = from_msr_tw_atom,
    .to_msr_pl = to_msr_pl_default,
    .from_msr_pl = from_msr_pl_default,
    .constraints = 2
  }
};

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

static off_t zone_to_msr_offset(raplcap_zone zone) {
  static const off_t ZONE_OFFSETS[NZONES] = {
    MSR_PKG_POWER_LIMIT,      // RAPLCAP_ZONE_PACKAGE
    MSR_PP0_POWER_LIMIT,      // RAPLCAP_ZONE_CORE
    MSR_PP1_POWER_LIMIT,      // RAPLCAP_ZONE_UNCORE
    MSR_DRAM_POWER_LIMIT,     // RAPLCAP_ZONE_DRAM
    MSR_PLATFORM_POWER_LIMIT  // RAPLCAP_ZONE_PSYS
  };
  if ((int) zone < 0 || (int) zone >= NZONES) {
    raplcap_log(ERROR, "zone_to_msr_offset: Unknown zone: %d\n", zone);
    errno = EINVAL;
    return -1;
  }
  return ZONE_OFFSETS[zone];
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
  uint32_t cpu_family, cpu_model;
  int err_save;
  // check that we recognize the CPU
  raplcap_cpuid_get_family_model(&cpu_family, &cpu_model);
  if (!raplcap_cpuid_is_vendor_intel() || !raplcap_cpuid_is_cpu_supported(cpu_family, cpu_model)) {
    raplcap_log(ERROR, "raplcap_init: CPU not supported: Family=%"PRIu32", Model=%02X\n", cpu_family, cpu_model);
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
  // TODO: Check MSR_PKG_POWER_INFO for min/max values of PACKAGE domain (not all fields available on all processors)?
  // now populate state with unit conversions and function pointers
  state->cpu_model = cpu_model;
  switch (cpu_model) {
    case CPUID_MODEL_SANDYBRIDGE:
    case CPUID_MODEL_SANDYBRIDGE_X:
    //
    case CPUID_MODEL_IVYBRIDGE:
    case CPUID_MODEL_IVYBRIDGE_X:
    //
    case CPUID_MODEL_HASWELL_CORE:
    case CPUID_MODEL_HASWELL_X:
    case CPUID_MODEL_HASWELL_ULT:
    case CPUID_MODEL_HASWELL_GT3E:
    //
    case CPUID_MODEL_BROADWELL_CORE:
    case CPUID_MODEL_BROADWELL_GT3E:
    case CPUID_MODEL_BROADWELL_X:
    case CPUID_MODEL_BROADWELL_XEON_D:
    //
    case CPUID_MODEL_SKYLAKE_MOBILE:
    case CPUID_MODEL_SKYLAKE_DESKTOP:
    case CPUID_MODEL_SKYLAKE_X:
    //
    case CPUID_MODEL_KABYLAKE_MOBILE:
    case CPUID_MODEL_KABYLAKE_DESKTOP:
    //
    case CPUID_MODEL_ATOM_GOLDMONT:
    case CPUID_MODEL_ATOM_GEMINI_LAKE:
    case CPUID_MODEL_ATOM_DENVERTON:
    //
    case CPUID_MODEL_XEON_PHI_KNL:
    case CPUID_MODEL_XEON_PHI_KNM:
      state->power_units = from_msr_pu_default(msrval);
      state->time_units = from_msr_tu_default(msrval);
      state->cfg = CFG_DEFAULT;
      break;
    //----
    case CPUID_MODEL_ATOM_SILVERMONT1:
    case CPUID_MODEL_ATOM_MERRIFIELD:
    case CPUID_MODEL_ATOM_MOOREFIELD:
      state->power_units = from_msr_pu_atom(msrval);
      state->time_units = from_msr_tu_default(msrval);
      state->cfg = CFG_ATOM;
      break;
    case CPUID_MODEL_ATOM_AIRMONT:
      state->power_units = from_msr_pu_atom(msrval);
      state->time_units = from_msr_tu_default(msrval);
      state->cfg = CFG_ATOM_AIRMONT;
      break;
    //----
    default:
      raplcap_log(ERROR, "raplcap_init: Unknown architecture\n");
      raplcap_log(ERROR, "raplcap_init: Please report a bug if you see this message, it should never occur!\n");
      assert(0);
      break;
  }
  raplcap_log(DEBUG, "raplcap_init: model=%02X, power_units=%.12f, time_units=%.12f\n",
              state->cpu_model, state->power_units, state->time_units);
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

static int is_zone_enabled(const raplcap* rc, uint32_t socket, raplcap_zone zone, int silent) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone);
  int ret;
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, silent)) {
    return -1;
  }
  ret = get_bits(msrval, 15, 15) == 0x1 &&
        (state->cfg[zone].constraints > 1 ? get_bits(msrval, 47, 47) == 0x1 : 1);
  if (ret && !silent &&
      (get_bits(msrval, 16, 16) == 0x0 ||
       (state->cfg[zone].constraints > 1 && get_bits(msrval, 48, 48) == 0x0))) {
    raplcap_log(INFO, "Zone is enabled but clamping is not\n");
  }
  raplcap_log(DEBUG, "is_zone_enabled: socket=%"PRIu32", zone=%d, enabled=%d\n", socket, zone, ret);
  return ret;
}

int raplcap_is_zone_enabled(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  return is_zone_enabled(rc, socket, zone, 0);
}

int raplcap_is_zone_supported(const raplcap* rc, uint32_t socket, raplcap_zone zone) {
  int ret = is_zone_enabled(rc, socket, zone, 1);
  // I/O error indicates zone is not supported, otherwise it's some other error (e.g. EINVAL)
  if (ret == 0) {
    ret = 1;
  } else if (ret < 0) {
    if (errno == EIO) {
      ret = 0;
    } else if (errno != EINVAL) {
      raplcap_perror(ERROR, "raplcap_is_zone_supported: is_zone_enabled");
    }
  }
  raplcap_log(DEBUG, "raplcap_is_zone_supported: socket=%"PRIu32", zone=%d, supported=%d\n", socket, zone, ret);
  return ret;
}

// Enables or disables both the "enable" and "clamping" bits for all constraints
int raplcap_set_zone_enabled(const raplcap* rc, uint32_t socket, raplcap_zone zone, int enabled) {
  uint64_t msrval;
  const uint64_t enabled_bits = enabled ? 0x1 : 0x0;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone);
  int ret;
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  msrval = replace_bits(msrval, enabled_bits, 15, 15);
  if (state->cfg[zone].constraints > 1) {
    msrval = replace_bits(msrval, enabled_bits, 47, 47);
  }
  raplcap_log(DEBUG, "raplcap_set_zone_enabled: socket=%"PRIu32", zone=%d, enabled=%d\n", socket, zone, enabled);
  if ((ret = write_msr_by_offset(state->fds[socket], msr, msrval, 0)) == 0) {
    // try to enable clamping (not supported by all zones or all CPUs)
    msrval = replace_bits(msrval, enabled_bits, 16, 16);
    if (state->cfg[zone].constraints > 1) {
      msrval = replace_bits(msrval, enabled_bits, 48, 48);
    }
    if (write_msr_by_offset(state->fds[socket], msr, msrval, 1)) {
      raplcap_log(INFO, "Clamping not available for this zone or platform\n");
    }
  }
  return ret;
}

static void to_raplcap(raplcap_limit* limit, double seconds, double watts) {
  assert(limit != NULL);
  limit->seconds = seconds;
  limit->watts = watts;
}

int raplcap_get_limits(const raplcap* rc, uint32_t socket, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  double watts;
  double seconds;
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  if (limit_long != NULL) {
    watts = state->cfg[zone].from_msr_pl(get_bits(msrval, 0, 14), state->power_units);
    seconds = state->cfg[zone].from_msr_tw(get_bits(msrval, 17, 23), state->time_units);
    to_raplcap(limit_long, seconds, watts);
    raplcap_log(DEBUG, "raplcap_get_limits: socket=%"PRIu32", zone=%d, long_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, limit_long->seconds, limit_long->watts);
  }
  if (limit_short != NULL && state->cfg[zone].constraints > 1) {
    watts = state->cfg[zone].from_msr_pl(get_bits(msrval, 32, 46), state->power_units);
    if (zone == RAPLCAP_ZONE_PSYS) {
      raplcap_log(DEBUG, "raplcap_get_limits: Documentation does not specify PSys/Platform short term time window\n");
    }
    seconds = state->cfg[zone].from_msr_tw(get_bits(msrval, 49, 55), state->time_units);
    to_raplcap(limit_short, seconds, watts);
    raplcap_log(DEBUG, "raplcap_get_limits: socket=%"PRIu32", zone=%d, short_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, limit_short->seconds, limit_short->watts);
  }
  return 0;
}

int raplcap_set_limits(const raplcap* rc, uint32_t socket, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  uint64_t msrval;
  const raplcap_msr* state = get_state(socket, rc);
  const off_t msr = zone_to_msr_offset(zone);
  if (state == NULL || msr < 0 || read_msr_by_offset(state->fds[socket], msr, &msrval, 0)) {
    return -1;
  }
  if (limit_long != NULL) {
    raplcap_log(DEBUG, "raplcap_set_limits: socket=%"PRIu32", zone=%d, long_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, limit_long->seconds, limit_long->watts);
    if (limit_long->watts > 0) {
      msrval = replace_bits(msrval, state->cfg[zone].to_msr_pl(limit_long->watts, state->power_units), 0, 14);
    }
    if (limit_long->seconds > 0) {
      msrval = replace_bits(msrval, state->cfg[zone].to_msr_tw(limit_long->seconds, state->time_units), 17, 23);
    }
  }
  if (limit_short != NULL && state->cfg[zone].constraints > 1) {
    raplcap_log(DEBUG, "raplcap_set_limits: socket=%"PRIu32", zone=%d, short_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                socket, zone, limit_short->seconds, limit_short->watts);
    if (limit_short->watts > 0) {
      msrval = replace_bits(msrval, state->cfg[zone].to_msr_pl(limit_short->watts, state->power_units), 32, 46);
    }
    if (limit_short->seconds > 0) {
      // 14.9.3: This field may have a hard-coded value in hardware and ignores values written by software.
      if (zone == RAPLCAP_ZONE_PSYS) {
        // Table 35-37: PSYS has power limit #2, but time window #2 is not specified
        raplcap_log(WARN, "Not allowed to set PSys/Platform short term time window\n");
      } else {
        msrval = replace_bits(msrval, state->cfg[zone].to_msr_tw(limit_short->seconds, state->time_units), 49, 55);
      }
    }
  }
  return write_msr_by_offset(state->fds[socket], msr, msrval, 0);
}
