/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * RAPL implementation of powercap.
 *
 * @author Connor Imes
 * @date 2016-05-12
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <powercap.h>
#include <powercap-sysfs.h>

#include "powercap-intel-rapl.h"
#include "raplcap-common.h"

#ifndef MAX_NAME_SIZE
  #define MAX_NAME_SIZE 64
#endif

#define CONTROL_TYPE "intel-rapl"

#define CONSTRAINT_NAME_LONG "long_term"
#define CONSTRAINT_NAME_SHORT "short_term"
#define CONSTRAINT_NAME_PEAK "peak_power"

#define ZONE_NAME_PREFIX_PKG "package"
#define ZONE_NAME_CORE "core"
#define ZONE_NAME_UNCORE "uncore"
#define ZONE_NAME_DRAM "dram"
#define ZONE_NAME_PSYS "psys"


// like open(2), but returns 0 on ENOENT (No such file or directory)
static int maybe_open_zone_file(powercap_zone* pz, const char* ct_name, const uint32_t* zones, uint32_t depth,
                                powercap_zone_file type, int flags) {
  int fd = powercap_zone_file_open(pz, type, ct_name, zones, depth, flags);
  return (fd < 0 && errno == ENOENT) ? 0 : fd;
}

// like open(2), but returns 0 on ENOENT (No such file or directory)
static int maybe_open_constraint_file(powercap_constraint* pc, const char* ct_name, const uint32_t* zones,
                                      uint32_t depth, uint32_t constraint, powercap_constraint_file type, int flags) {
  int fd = powercap_constraint_file_open(pc, type, ct_name, zones, depth, constraint, flags);
  return (fd < 0 && errno == ENOENT) ? 0 : fd;
}

static int powercap_zone_open(powercap_zone* pz, const char* ct_name, const uint32_t* zones, uint32_t depth, int ro) {
  return maybe_open_zone_file(pz, ct_name, zones, depth,
                              POWERCAP_ZONE_FILE_MAX_ENERGY_RANGE_UJ, O_RDONLY) < 0 ||
         // special case for energy_uj - it's allowed to be either RW or RO
         (
          maybe_open_zone_file(pz, ct_name, zones, depth,
                               POWERCAP_ZONE_FILE_ENERGY_UJ, ro ? O_RDONLY : O_RDWR) < 0 &&
          (ro || maybe_open_zone_file(pz, ct_name, zones, depth,
                                      POWERCAP_ZONE_FILE_ENERGY_UJ, O_RDONLY < 0))
         ) ||
         maybe_open_zone_file(pz, ct_name, zones, depth,
                              POWERCAP_ZONE_FILE_MAX_POWER_RANGE_UW, O_RDONLY) < 0 ||
         maybe_open_zone_file(pz, ct_name, zones, depth,
                              POWERCAP_ZONE_FILE_POWER_UW, O_RDONLY) < 0 ||
         maybe_open_zone_file(pz, ct_name, zones, depth,
                              POWERCAP_ZONE_FILE_ENABLED, ro ? O_RDONLY : O_RDWR) < 0 ||
         maybe_open_zone_file(pz, ct_name, zones, depth,
                              POWERCAP_ZONE_FILE_NAME, O_RDONLY) < 0
         ? -1 : 0;
}

static int powercap_constraint_open(powercap_constraint* pc, const char* ct_name, const uint32_t* zones, uint32_t depth,
                                    uint32_t constraint, int ro) {
  return maybe_open_constraint_file(pc, ct_name, zones, depth, constraint,
                                    POWERCAP_CONSTRAINT_FILE_POWER_LIMIT_UW, ro ? O_RDONLY : O_RDWR) < 0 ||
         maybe_open_constraint_file(pc, ct_name, zones, depth, constraint,
                                    POWERCAP_CONSTRAINT_FILE_TIME_WINDOW_US, ro ? O_RDONLY : O_RDWR) < 0 ||
         maybe_open_constraint_file(pc, ct_name, zones, depth, constraint,
                                    POWERCAP_CONSTRAINT_FILE_MAX_POWER_UW, O_RDONLY) < 0 ||
         maybe_open_constraint_file(pc, ct_name, zones, depth, constraint,
                                    POWERCAP_CONSTRAINT_FILE_MIN_POWER_UW, O_RDONLY) < 0 ||
         maybe_open_constraint_file(pc, ct_name, zones, depth, constraint,
                                    POWERCAP_CONSTRAINT_FILE_MAX_TIME_WINDOW_US, O_RDONLY) < 0 ||
         maybe_open_constraint_file(pc, ct_name, zones, depth, constraint,
                                    POWERCAP_CONSTRAINT_FILE_MIN_TIME_WINDOW_US, O_RDONLY) < 0 ||
         maybe_open_constraint_file(pc, ct_name, zones, depth, constraint,
                                    POWERCAP_CONSTRAINT_FILE_NAME, O_RDONLY) < 0
         ? -1 : 0;
}

static int powercap_close(int fd) {
  return (fd > 0 && close(fd)) ? -1 : 0;
}

static int powercap_zone_close(powercap_zone* pz) {
  int rc = 0;
  rc |= powercap_close(pz->max_energy_range_uj);
  rc |= powercap_close(pz->energy_uj);
  rc |= powercap_close(pz->max_power_range_uw);
  rc |= powercap_close(pz->power_uw);
  rc |= powercap_close(pz->enabled);
  rc |= powercap_close(pz->name);
  return rc;
}

static int powercap_constraint_close(powercap_constraint* pc) {
  int rc = 0;
  rc |= powercap_close(pc->power_limit_uw);
  rc |= powercap_close(pc->time_window_us);
  rc |= powercap_close(pc->max_power_uw);
  rc |= powercap_close(pc->min_power_uw);
  rc |= powercap_close(pc->max_time_window_us);
  rc |= powercap_close(pc->min_time_window_us);
  rc |= powercap_close(pc->name);
  return rc;
}

static powercap_constraint* get_constraint_by_rapl_name(powercap_intel_rapl_zone_files* fds, const uint32_t* zones, uint32_t depth, uint32_t constraint) {
  assert(fds != NULL);
  char name[MAX_NAME_SIZE];
  if (powercap_sysfs_constraint_get_name(CONTROL_TYPE, zones, depth, constraint, name, sizeof(name)) < 0) {
    if (depth == 1) {
      raplcap_log(ERROR,
                  "powercap-intel-rapl: Failed to get constraint name at zone: %"PRIu32", constraint: %"PRIu32"\n",
                  zones[0], constraint);
    } else {
      raplcap_log(ERROR,
                  "powercap-intel-rapl: Failed to get constraint name at zone: %"PRIu32":%"PRIu32", constraint: %"PRIu32"\n",
                  zones[0], zones[1], constraint);
    }
    return NULL;
  }
  if (!strncmp(name, CONSTRAINT_NAME_LONG, sizeof(CONSTRAINT_NAME_LONG))) {
    return &fds->constraints[RAPLCAP_CONSTRAINT_LONG_TERM];
  } else if (!strncmp(name, CONSTRAINT_NAME_SHORT, sizeof(CONSTRAINT_NAME_SHORT))) {
    return &fds->constraints[RAPLCAP_CONSTRAINT_SHORT_TERM];
  } else if (!strncmp(name, CONSTRAINT_NAME_PEAK, sizeof(CONSTRAINT_NAME_PEAK))) {
    return &fds->constraints[RAPLCAP_CONSTRAINT_PEAK_POWER];
  } else {
    raplcap_log(ERROR, "powercap-intel-rapl: Unrecognized constraint name: %s\n", name);
    errno = EINVAL;
  }
  return NULL;
}

static int open_all(const uint32_t* zones, uint32_t depth, powercap_intel_rapl_zone_files* fds, int ro) {
  assert(fds != NULL);
  powercap_constraint* pc;
  uint32_t i = 0;
  if (powercap_zone_open(&fds->zone, CONTROL_TYPE, zones, depth, ro)) {
    raplcap_perror(ERROR, "powercap-intel-rapl: powercap_zone_open");
    return -errno;
  }
  errno = 0;
  // constraint 0 is supposed to be long_term and constraint 1 (if exists) should be short_term
  // note: never actually seen this problem, but not 100% sure it can't happen, so check anyway...
  while (!powercap_sysfs_constraint_exists(CONTROL_TYPE, zones, depth, i)) {
    if ((pc = get_constraint_by_rapl_name(fds, zones, depth, i)) == NULL) {
      return -errno;
    }
    // "power_limit_uw" is picked arbitrarily, but it is a required file
    if (pc->power_limit_uw) {
      if (depth == 1) {
        raplcap_log(ERROR, "powercap-intel-rapl: Duplicate constraint detected at zone: %"PRIu32"\n", zones[0]);
      } else {
        raplcap_log(ERROR, "powercap-intel-rapl: Duplicate constraint detected at zone: %"PRIu32":%"PRIu32"\n", zones[0], zones[1]);
      }
      errno = EINVAL;
      return -errno;
    }
    if (powercap_constraint_open(pc, CONTROL_TYPE, zones, depth, i, ro)) {
      raplcap_perror(ERROR, "powercap-intel-rapl: powercap_constraint_open");
      return -errno;
    }
    i++;
  }
  // powercap_sysfs_constraint_exists returns error code when constraint does not exist - make sure it's not our fault
  assert(errno != EINVAL);
  assert(errno != ENOBUFS);
  return 0;
}

static int get_zone_fd(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_zone_file file) {
  assert((int) file >= 0 && (int) file <= POWERCAP_ZONE_FILE_NAME);
  switch (file) {
    case POWERCAP_ZONE_FILE_MAX_ENERGY_RANGE_UJ:
      return parent->zones[zone].zone.max_energy_range_uj;
    case POWERCAP_ZONE_FILE_ENERGY_UJ:
      return parent->zones[zone].zone.energy_uj;
    case POWERCAP_ZONE_FILE_MAX_POWER_RANGE_UW:
      return parent->zones[zone].zone.max_power_range_uw;
    case POWERCAP_ZONE_FILE_POWER_UW:
      return parent->zones[zone].zone.power_uw;
    case POWERCAP_ZONE_FILE_ENABLED:
      return parent->zones[zone].zone.enabled;
    case POWERCAP_ZONE_FILE_NAME:
      return parent->zones[zone].zone.name;
    default:
      // unreachable
      raplcap_log(ERROR, "powercap-intel-rapl: Bad powercap_zone_file: %d\n", file);
      errno = EINVAL;
      return -errno;
  }
}

static int get_constraint_fd(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint, powercap_constraint_file file) {
  assert((int) file >= 0 && (int) file <= POWERCAP_CONSTRAINT_FILE_NAME);
  switch (file) {
    case POWERCAP_CONSTRAINT_FILE_POWER_LIMIT_UW:
      return parent->zones[zone].constraints[constraint].power_limit_uw;
    case POWERCAP_CONSTRAINT_FILE_TIME_WINDOW_US:
      return parent->zones[zone].constraints[constraint].time_window_us;
    case POWERCAP_CONSTRAINT_FILE_MAX_POWER_UW:
      return parent->zones[zone].constraints[constraint].max_power_uw;
    case POWERCAP_CONSTRAINT_FILE_MIN_POWER_UW:
      return parent->zones[zone].constraints[constraint].min_power_uw;
    case POWERCAP_CONSTRAINT_FILE_MAX_TIME_WINDOW_US:
      return parent->zones[zone].constraints[constraint].max_time_window_us;
    case POWERCAP_CONSTRAINT_FILE_MIN_TIME_WINDOW_US:
      return parent->zones[zone].constraints[constraint].min_time_window_us;
    case POWERCAP_CONSTRAINT_FILE_NAME:
      return parent->zones[zone].constraints[constraint].name;
    default:
      // unreachable
      raplcap_log(ERROR, "powercap-intel-rapl: Bad powercap_constraint_file: %d\n", file);
      errno = EINVAL;
      return -errno;
  }
}

static powercap_intel_rapl_zone_files* get_files_by_name(powercap_intel_rapl_parent* parent, const uint32_t* zones, uint32_t depth) {
  assert(parent != NULL);
  char name[MAX_NAME_SIZE];
  if (powercap_sysfs_zone_get_name(CONTROL_TYPE, zones, depth, name, sizeof(name)) < 0) {
    return NULL;
  }
  if (!strncmp(name, ZONE_NAME_PREFIX_PKG, sizeof(ZONE_NAME_PREFIX_PKG) - 1)) {
    return &parent->zones[RAPLCAP_ZONE_PACKAGE];
  } else if (!strncmp(name, ZONE_NAME_CORE, sizeof(ZONE_NAME_CORE))) {
    return &parent->zones[RAPLCAP_ZONE_CORE];
  } else if (!strncmp(name, ZONE_NAME_UNCORE, sizeof(ZONE_NAME_UNCORE))) {
    return &parent->zones[RAPLCAP_ZONE_UNCORE];
  } else if (!strncmp(name, ZONE_NAME_DRAM, sizeof(ZONE_NAME_DRAM))) {
    return &parent->zones[RAPLCAP_ZONE_DRAM];
  } else if (!strncmp(name, ZONE_NAME_PSYS, sizeof(ZONE_NAME_PSYS))) {
    return &parent->zones[RAPLCAP_ZONE_PSYS];
  } else {
    raplcap_log(ERROR, "powercap-intel-rapl: Unrecognized zone name: %s\n", name);
    errno = EINVAL;
  }
  return NULL;
}

uint32_t powercap_intel_rapl_get_num_instances(void) {
  uint32_t n = 0;
  while (!powercap_sysfs_zone_exists(CONTROL_TYPE, &n, 1)) {
    n++;
  }
  if (!n) {
    raplcap_log(ERROR, "powercap-intel-rapl: No top-level "CONTROL_TYPE" zones found - is its kernel module loaded?\n");
    errno = ENOENT;
  }
  return n;
}

int powercap_intel_rapl_init(uint32_t id, powercap_intel_rapl_parent* parent, int read_only) {
  int ret;
  int err_save;
  uint32_t zones[2] = { id, 0 };
  powercap_intel_rapl_zone_files* files;
  if (parent == NULL) {
    errno = EINVAL;
    return -errno;
  }
  // first need the parent zone
  if ((files = get_files_by_name(parent, zones, 1)) == NULL) {
    return -errno;
  }
  // force all fds to 0 so we don't try to operate on invalid descriptors
  memset(parent, 0, sizeof(*parent));
  // first populate parent zone
  if (!(ret = open_all(zones, 1, files, read_only))) {
    // get subordinate power zones
    while(!powercap_sysfs_zone_exists(CONTROL_TYPE, zones, 2) && !ret) {
      if ((files = get_files_by_name(parent, zones, 2)) == NULL) {
        ret = -errno;
      } else if (files->zone.name) {
        // zone has already been opened ("name" is picked arbitrarily, but it is a required file)
        raplcap_log(ERROR, "powercap-intel-rapl: Duplicate zone type detected at %"PRIu32":%"PRIu32"\n", zones[0], zones[1]);
        errno = EBUSY;
        ret = -errno;
      } else {
        ret = open_all(zones, 2, files, read_only);
        zones[1]++;
      }
    }
  }
  if (ret) {
    err_save = errno;
    powercap_intel_rapl_destroy(parent);
    errno = err_save;
  }
  return ret;
}

static int fds_destroy_all(powercap_intel_rapl_zone_files* files) {
  assert(files != NULL);
  size_t i;
  int ret = 0;
  ret |= powercap_zone_close(&files->zone);
  for (i = 0; i < RAPLCAP_NCONSTRAINTS; i++) {
    ret |= powercap_constraint_close(&files->constraints[i]);
  }
  return ret;
}

int powercap_intel_rapl_destroy(powercap_intel_rapl_parent* parent) {
  size_t i;
  int ret = 0;
  if (parent != NULL) {
    for (i = 0; i < RAPLCAP_NZONES; i++) {
      ret |= fds_destroy_all(&parent->zones[i]);
    }
  }
  return ret;
}

int powercap_intel_rapl_is_zone_supported(const powercap_intel_rapl_parent* parent, raplcap_zone zone) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  // POWERCAP_ZONE_FILE_NAME is picked arbitrarily, but it is a required file
  return get_zone_fd(parent, zone, POWERCAP_ZONE_FILE_NAME) > 0 ? 1 : 0;
}

int powercap_intel_rapl_is_constraint_supported(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  assert((int) constraint >= 0 && (int) constraint < RAPLCAP_NCONSTRAINTS);
  // POWERCAP_CONSTRAINT_FILE_POWER_LIMIT_UW is picked arbitrarily, but it is a required file
  return get_constraint_fd(parent, zone, constraint, POWERCAP_CONSTRAINT_FILE_POWER_LIMIT_UW) > 0 ? 1 : 0;
}

ssize_t powercap_intel_rapl_get_name(const powercap_intel_rapl_parent* parent, raplcap_zone zone, char* buf, size_t size) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  return powercap_zone_get_name(&parent->zones[zone].zone, buf, size);
}

int powercap_intel_rapl_is_enabled(const powercap_intel_rapl_parent* parent, raplcap_zone zone) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  int enabled = -1;
  int ret;
  if ((ret = powercap_zone_get_enabled(&parent->zones[zone].zone, &enabled))) {
    enabled = ret;
  }
  return enabled;
}

int powercap_intel_rapl_set_enabled(const powercap_intel_rapl_parent* parent, raplcap_zone zone, int enabled) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  return powercap_zone_set_enabled(&parent->zones[zone].zone, enabled);
}

int powercap_intel_rapl_get_max_energy_range_uj(const powercap_intel_rapl_parent* parent, raplcap_zone zone, uint64_t* val) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  return powercap_zone_get_max_energy_range_uj(&parent->zones[zone].zone, val);
}

int powercap_intel_rapl_get_energy_uj(const powercap_intel_rapl_parent* parent, raplcap_zone zone, uint64_t* val) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  return powercap_zone_get_energy_uj(&parent->zones[zone].zone, val);
}

int powercap_intel_rapl_get_power_limit_uw(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint, uint64_t* val) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  assert((int) constraint >= 0 && (int) constraint < RAPLCAP_NCONSTRAINTS);
  return powercap_constraint_get_power_limit_uw(&parent->zones[zone].constraints[constraint], val);
}

int powercap_intel_rapl_set_power_limit_uw(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint, uint64_t val) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  assert((int) constraint >= 0 && (int) constraint < RAPLCAP_NCONSTRAINTS);
  return powercap_constraint_set_power_limit_uw(&parent->zones[zone].constraints[constraint], val);
}

int powercap_intel_rapl_get_time_window_us(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint, uint64_t* val) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  assert((int) constraint >= 0 && (int) constraint < RAPLCAP_NCONSTRAINTS);
  return powercap_constraint_get_time_window_us(&parent->zones[zone].constraints[constraint], val);
}

int powercap_intel_rapl_set_time_window_us(const powercap_intel_rapl_parent* parent, raplcap_zone zone, raplcap_constraint constraint, uint64_t val) {
  assert(parent);
  assert((int) zone >= 0 && (int) zone < RAPLCAP_NZONES);
  assert((int) constraint >= 0 && (int) constraint < RAPLCAP_NCONSTRAINTS);
  return powercap_constraint_set_time_window_us(&parent->zones[zone].constraints[constraint], val);
}
