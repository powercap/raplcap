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
    return NULL;
  }
  if (!strncmp(name, CONSTRAINT_NAME_LONG, sizeof(CONSTRAINT_NAME_LONG))) {
    return &fds->constraint_long;
  } else if (!strncmp(name, CONSTRAINT_NAME_SHORT, sizeof(CONSTRAINT_NAME_SHORT))) {
    return &fds->constraint_short;
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

static const powercap_intel_rapl_zone_files* get_files(const powercap_intel_rapl_parent* parent, raplcap_zone zone) {
  assert(parent != NULL);
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
      return &parent->pkg;
    case RAPLCAP_ZONE_CORE:
      return &parent->core;
    case RAPLCAP_ZONE_UNCORE:
      return &parent->uncore;
    case RAPLCAP_ZONE_DRAM:
      return &parent->dram;
    case RAPLCAP_ZONE_PSYS:
      return &parent->psys;
    default:
      // somebody passed a bad zone type
      raplcap_log(ERROR, "powercap-intel-rapl: Bad powercap_intel_rapl_zone: %d\n", zone);
      errno = EINVAL;
      return NULL;
  }
}

static const powercap_zone* get_zone_files(const powercap_intel_rapl_parent* parent, raplcap_zone zone) {
  assert(parent != NULL);
  const powercap_intel_rapl_zone_files* fds = get_files(parent, zone);
  return fds == NULL ? NULL : &fds->zone;
}

static const powercap_constraint* get_constraint_files(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_intel_rapl_constraint constraint) {
  assert(parent != NULL);
  const powercap_intel_rapl_zone_files* fds = get_files(parent, zone);
  if (fds == NULL) {
    return NULL;
  }
  switch (constraint) {
    case POWERCAP_INTEL_RAPL_CONSTRAINT_LONG:
      return &fds->constraint_long;
    case POWERCAP_INTEL_RAPL_CONSTRAINT_SHORT:
      return &fds->constraint_short;
    default:
      // somebody passed a bad constraint type
      raplcap_log(ERROR, "powercap-intel-rapl: Bad powercap_intel_rapl_constraint: %d\n", constraint);
      errno = EINVAL;
      return NULL;
  }
}

static int get_zone_fd(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_zone_file file) {
  assert(parent != NULL);
  const powercap_zone* fds = get_zone_files(parent, zone);
  if (fds == NULL) {
    return -errno;
  }
  switch (file) {
    case POWERCAP_ZONE_FILE_MAX_ENERGY_RANGE_UJ:
      return fds->max_energy_range_uj;
    case POWERCAP_ZONE_FILE_ENERGY_UJ:
      return fds->energy_uj;
    case POWERCAP_ZONE_FILE_MAX_POWER_RANGE_UW:
      return fds->max_power_range_uw;
    case POWERCAP_ZONE_FILE_POWER_UW:
      return fds->power_uw;
    case POWERCAP_ZONE_FILE_ENABLED:
      return fds->enabled;
    case POWERCAP_ZONE_FILE_NAME:
      return fds->name;
    default:
      raplcap_log(ERROR, "powercap-intel-rapl: Bad powercap_zone_file: %d\n", file);
      errno = EINVAL;
      return -errno;
  }
}

static int get_constraint_fd(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_intel_rapl_constraint constraint, powercap_constraint_file file) {
  assert(parent != NULL);
  const powercap_constraint* fds = get_constraint_files(parent, zone, constraint);
  if (fds == NULL) {
    return -errno;
  }
  switch (file) {
    case POWERCAP_CONSTRAINT_FILE_POWER_LIMIT_UW:
      return fds->power_limit_uw;
    case POWERCAP_CONSTRAINT_FILE_TIME_WINDOW_US:
      return fds->time_window_us;
    case POWERCAP_CONSTRAINT_FILE_MAX_POWER_UW:
      return fds->max_power_uw;
    case POWERCAP_CONSTRAINT_FILE_MIN_POWER_UW:
      return fds->min_power_uw;
    case POWERCAP_CONSTRAINT_FILE_MAX_TIME_WINDOW_US:
      return fds->max_time_window_us;
    case POWERCAP_CONSTRAINT_FILE_MIN_TIME_WINDOW_US:
      return fds->min_time_window_us;
    case POWERCAP_CONSTRAINT_FILE_NAME:
      return fds->name;
    default:
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
    return &parent->pkg;
  } else if (!strncmp(name, ZONE_NAME_CORE, sizeof(ZONE_NAME_CORE))) {
    return &parent->core;
  } else if (!strncmp(name, ZONE_NAME_UNCORE, sizeof(ZONE_NAME_UNCORE))) {
    return &parent->uncore;
  } else if (!strncmp(name, ZONE_NAME_DRAM, sizeof(ZONE_NAME_DRAM))) {
    return &parent->dram;
  } else if (!strncmp(name, ZONE_NAME_PSYS, sizeof(ZONE_NAME_PSYS))) {
    return &parent->psys;
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
  int ret = 0;
  ret |= powercap_zone_close(&files->zone);
  ret |= powercap_constraint_close(&files->constraint_long);
  ret |= powercap_constraint_close(&files->constraint_short);
  return ret;
}

int powercap_intel_rapl_destroy(powercap_intel_rapl_parent* parent) {
  int ret = 0;
  if (parent != NULL) {
    ret |= fds_destroy_all(&parent->pkg);
    ret |= fds_destroy_all(&parent->core);
    ret |= fds_destroy_all(&parent->uncore);
    ret |= fds_destroy_all(&parent->dram);
    ret |= fds_destroy_all(&parent->psys);
  }
  return ret;
}

static int powercap_intel_rapl_is_zone_file_supported(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_zone_file file) {
  int fd;
  if (parent == NULL || (fd = get_zone_fd(parent, zone, file)) < 0) {
    errno = EINVAL;
    return -errno;
  }
  return fd > 0 ? 1 : 0;
}

int powercap_intel_rapl_is_zone_supported(const powercap_intel_rapl_parent* parent, raplcap_zone zone) {
  // POWERCAP_ZONE_FILE_NAME is picked arbitrarily, but it is a required file
  return powercap_intel_rapl_is_zone_file_supported(parent, zone, POWERCAP_ZONE_FILE_NAME);
}

static int powercap_intel_rapl_is_constraint_file_supported(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_intel_rapl_constraint constraint, powercap_constraint_file file) {
  int fd;
  if (parent == NULL || (fd = get_constraint_fd(parent, zone, constraint, file)) < 0) {
    errno = EINVAL;
    return -errno;
  }
  return fd > 0 ? 1 : 0;
}

int powercap_intel_rapl_is_constraint_supported(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_intel_rapl_constraint constraint) {
  // POWERCAP_CONSTRAINT_FILE_POWER_LIMIT_UW is picked arbitrarily, but it is a required file
  return powercap_intel_rapl_is_constraint_file_supported(parent, zone, constraint, POWERCAP_CONSTRAINT_FILE_POWER_LIMIT_UW);
}

ssize_t powercap_intel_rapl_get_name(const powercap_intel_rapl_parent* parent, raplcap_zone zone, char* buf, size_t size) {
  const powercap_zone* fds = get_zone_files(parent, zone);
  return fds == NULL ? -errno : powercap_zone_get_name(fds, buf, size);
}

int powercap_intel_rapl_is_enabled(const powercap_intel_rapl_parent* parent, raplcap_zone zone) {
  int enabled = -1;
  int ret;
  const powercap_zone* fds = get_zone_files(parent, zone);
  if (fds == NULL) {
    enabled = -errno;
  } else if ((ret = powercap_zone_get_enabled(fds, &enabled))) {
    enabled = ret;
  }
  return enabled;
}

int powercap_intel_rapl_set_enabled(const powercap_intel_rapl_parent* parent, raplcap_zone zone, int enabled) {
  const powercap_zone* fds = get_zone_files(parent, zone);
  return fds == NULL ? -errno : powercap_zone_set_enabled(fds, enabled);
}

int powercap_intel_rapl_get_max_energy_range_uj(const powercap_intel_rapl_parent* parent, raplcap_zone zone, uint64_t* val) {
  const powercap_zone* fds = get_zone_files(parent, zone);
  return fds == NULL ? -errno : powercap_zone_get_max_energy_range_uj(fds, val);
}

int powercap_intel_rapl_get_energy_uj(const powercap_intel_rapl_parent* parent, raplcap_zone zone, uint64_t* val) {
  const powercap_zone* fds = get_zone_files(parent, zone);
  return fds == NULL ? -errno : powercap_zone_get_energy_uj(fds, val);
}

int powercap_intel_rapl_get_power_limit_uw(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_intel_rapl_constraint constraint, uint64_t* val) {
  const powercap_constraint* fds = get_constraint_files(parent, zone, constraint);
  return fds == NULL ? -errno : powercap_constraint_get_power_limit_uw(fds, val);
}

int powercap_intel_rapl_set_power_limit_uw(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_intel_rapl_constraint constraint, uint64_t val) {
  const powercap_constraint* fds = get_constraint_files(parent, zone, constraint);
  return fds == NULL ? -errno : powercap_constraint_set_power_limit_uw(fds, val);
}

int powercap_intel_rapl_get_time_window_us(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_intel_rapl_constraint constraint, uint64_t* val) {
  const powercap_constraint* fds = get_constraint_files(parent, zone, constraint);
  return fds == NULL ? -errno : powercap_constraint_get_time_window_us(fds, val);
}

int powercap_intel_rapl_set_time_window_us(const powercap_intel_rapl_parent* parent, raplcap_zone zone, powercap_intel_rapl_constraint constraint, uint64_t val) {
  const powercap_constraint* fds = get_constraint_files(parent, zone, constraint);
  return fds == NULL ? -errno : powercap_constraint_set_time_window_us(fds, val);
}
