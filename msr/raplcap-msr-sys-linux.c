/**
 * Linux MSR access.
 *
 * @author Connor Imes
 * @date 2020-06-09
 */
// for popen, pread, pwrite, sysconf
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "raplcap-common.h"
#include "raplcap-msr-sys.h"

struct raplcap_msr_sys_ctx {
  int* fds;
  uint32_t n_fds;
  uint32_t n_pkg;
  uint32_t n_die;
};

typedef struct msr_topology {
  uint32_t pkg;
  uint32_t die;
  uint32_t cpu;
} msr_topology;

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

static uint32_t get_cpu_count(void) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n <= 0 || n > UINT32_MAX) {
    errno = ENODEV;
    return 0;
  }
  return (uint32_t) n;
}

static int get_physical_package_id(uint32_t cpu, uint32_t* pkg) {
  char fname[92] = { 0 };
  FILE* f;
  int fret;
  // phys socket IDs may not be in range [0, nsockets), see kernel docs: Documentation/cputopology.txt
  snprintf(fname, sizeof(fname), "/sys/devices/system/cpu/cpu%"PRIu32"/topology/physical_package_id", cpu);
  if ((f = fopen(fname, "r")) == NULL) {
    raplcap_perror(ERROR, fname);
    return -1;
  }
  fret = fscanf(f, "%"PRIu32, pkg);
  if (fclose(f)) {
    raplcap_perror(WARN, "get_physical_package_id: fclose");
  }
  if (fret != 1) {
    raplcap_log(ERROR, "get_physical_package_id: Failed to read physical_package_id for cpu%"PRIu32"\n", cpu);
    errno = ENODATA;
    return -1;
  }
  raplcap_log(DEBUG, "get_physical_package_id: cpu=%"PRIu32", pkg=%"PRIu32"\n", cpu, *pkg);
  return 0;
}

static int get_die_id(uint32_t cpu, uint32_t* die) {
  char fname[92] = { 0 };
  struct stat ss;
  FILE* f;
  int fret;
  snprintf(fname, sizeof(fname), "/sys/devices/system/cpu/cpu%"PRIu32"/topology/die_id", cpu);
  // die_id does not exist on all systems, so check for it first
  if (stat(fname, &ss)) {
    raplcap_log(DEBUG, "get_die_id: %s: %s\n", fname, strerror(errno));
    *die = 0;
  } else {
    if ((f = fopen(fname, "r")) == NULL) {
      raplcap_perror(ERROR, fname);
      return -1;
    }
    fret = fscanf(f, "%"PRIu32, die);
    if (fclose(f)) {
      raplcap_perror(WARN, "get_die_id: fclose");
    }
    if (fret != 1) {
      raplcap_log(ERROR, "get_die_id: Failed to read die_id for cpu%"PRIu32"\n", cpu);
      errno = ENODATA;
      return -1;
    }
  }
  raplcap_log(DEBUG, "get_die_id: cpu=%"PRIu32", die=%"PRIu32"\n", cpu, *die);
  return 0;
}

static int get_topology(msr_topology* topo, uint32_t ncpus) {
  // assumes cpus are numbered from 0 to ncpus-1
  uint32_t i;
  for (i = 0; i < ncpus; i++) {
    if (get_physical_package_id(i, &topo[i].pkg) < 0) {
      return -1;
    }
    if (get_die_id(i, &topo[i].die) < 0) {
      return -1;
    }
    topo[i].cpu = i;
  }
  return 0;
}

static int cmp_u32(const void* a, const void* b) {
  return *((const uint32_t*) a) > *((const uint32_t*) b) ? 1 :
         ((*((const uint32_t*) a) < *((const uint32_t*) b)) ? -1 : 0);
}

static int cmp_msr_topology_pkg_die(const void* a, const void* b) {
  const msr_topology* ta = (const msr_topology*) a;
  const msr_topology* tb = (const msr_topology*) b;
  int rc = cmp_u32(&ta->pkg, &tb->pkg);
  return rc ? rc : cmp_u32(&ta->die, &tb->die);
}

// Count unique combinations of pkg and die in topo (must be pre-sorted).
static uint32_t count_unique_pkg_die(const msr_topology* topo, uint32_t n) {
  assert(n > 0);
  uint32_t unique = 1;
  uint32_t i;
  for (i = 1; i < n; i++) {
    if (cmp_msr_topology_pkg_die(&topo[i], &topo[i - 1])) {
      unique++;
    }
  }
  raplcap_log(DEBUG, "count_unique_pkg_die: unique=%"PRIu32"\n", unique);
  return unique;
}

// Determine which CPUs to open MSRs for based on topo (must be pre-sorted)
static void get_cpus_to_open(uint32_t* cpus_to_open, uint32_t n_cpus_to_open, const msr_topology* topo, uint32_t n_cpus) {
  uint32_t i;
  uint32_t j;
  assert(n_cpus > 0);
  cpus_to_open[0] = topo[0].cpu;
  for (i = 1, j = 1; i < n_cpus; i++) {
    if (cmp_msr_topology_pkg_die(&topo[i], &topo[i - 1])) {
      cpus_to_open[j++] = topo[i].cpu;
    }
  }
  assert(j == n_cpus_to_open);
  for (i = 0; i < n_cpus_to_open; i++) {
    raplcap_log(DEBUG, "get_cpus_to_open: cpu=%"PRIu32"\n", cpus_to_open[i]);
  }
}

// Note: doesn't close previously opened file descriptors if one fails to open
static int open_msrs(int* fds, const uint32_t* cpus_to_open, uint32_t n_fds) {
  uint32_t i;
  const char* env_ro = getenv(ENV_RAPLCAP_READ_ONLY);
  int ro = env_ro == NULL ? 0 : atoi(env_ro);
  for (i = 0; i < n_fds; i++) {
    if ((fds[i] = open_msr(cpus_to_open[i], ro == 0 ? O_RDWR : O_RDONLY)) < 0) {
      return -1;
    }
  }
  return 0;
}

int msr_get_num_pkg_die(const raplcap_msr_sys_ctx* ctx, uint32_t *n_pkg, uint32_t* n_die) {
  msr_topology* topo;
  uint32_t ncpus;
  assert(n_pkg);
  assert(n_die);
  if (ctx) {
    *n_pkg = ctx->n_pkg;
    *n_die = ctx->n_die;
    return 0;
  }
  if ((ncpus = get_cpu_count()) == 0) {
    raplcap_perror(ERROR, "msr_get_num_pkg_die: get_cpu_count");
    return -1;
  }
  if ((topo = malloc(ncpus * sizeof(*topo))) == NULL) {
    raplcap_perror(ERROR, "msr_get_num_pkg_die: malloc");
    return -1;
  }
  if (get_topology(topo, ncpus)) {
    free(topo);
    return -1;
  }
  qsort(topo, ncpus, sizeof(*topo), cmp_msr_topology_pkg_die);
  *n_pkg = topo[ncpus - 1].pkg + 1;
  // assumes homogeneous die configurations across packages
  *n_die = topo[ncpus - 1].die + 1;
  raplcap_log(DEBUG, "msr_get_num_pkg_die: n_cpus=%"PRIu32", n_pkg=%"PRIu32", n_die=%"PRIu32"\n", ncpus, *n_pkg, *n_die);
  free(topo);
  return 0;
}

raplcap_msr_sys_ctx* msr_sys_init(uint32_t* n_pkg, uint32_t* n_die) {
  msr_topology* topo;
  raplcap_msr_sys_ctx* ctx;
  uint32_t* cpus_to_open;
  uint32_t ncpus;
  int err_save;
  assert(n_pkg);
  assert(n_die);
  // need to decide which CPU MSRs to open to cover all RAPL zones
  if ((ncpus = get_cpu_count()) == 0) {
    raplcap_perror(ERROR, "msr_sys_init: get_cpu_count");
    return NULL;
  }
  if ((topo = malloc(ncpus * sizeof(*topo))) == NULL) {
    raplcap_perror(ERROR, "msr_sys_init: malloc");
    return NULL;
  }
  // get topology for all CPUs, sort by pkg and die, then count unique combinations to determine how many MSRs to open
  if (get_topology(topo, ncpus)) {
    free(topo);
    return NULL;
  }
  qsort(topo, ncpus, sizeof(*topo), cmp_msr_topology_pkg_die);
  if ((ctx = malloc(sizeof(*ctx))) == NULL) {
    raplcap_perror(ERROR, "msr_sys_init: malloc");
    free(topo);
    return NULL;
  }
  // assumes homogeneous die configurations across packages
  ctx->n_pkg = topo[ncpus - 1].pkg + 1;
  ctx->n_die = topo[ncpus - 1].die + 1;
  ctx->n_fds = count_unique_pkg_die(topo, ncpus);
  raplcap_log(DEBUG, "msr_sys_init: n_cpus=%"PRIu32", n_pkg=%"PRIu32", n_die=%"PRIu32", n_fds=%"PRIu32"\n",
              ncpus, ctx->n_pkg, ctx->n_die, ctx->n_fds);
  // now determine which CPUs to open MSRs for and do it
  if ((cpus_to_open = malloc(ctx->n_fds * sizeof(uint32_t))) == NULL) {
    raplcap_perror(ERROR, "msr_sys_init: malloc");
    free(ctx);
    free(topo);
    return NULL;
  }
  get_cpus_to_open(cpus_to_open, ctx->n_fds, topo, ncpus);
  if ((ctx->fds = calloc(ctx->n_fds, sizeof(int))) == NULL) {
    raplcap_perror(ERROR, "msr_sys_init: calloc");
    free(cpus_to_open);
    free(ctx);
    free(topo);
    return NULL;
  }
  if (open_msrs(ctx->fds, cpus_to_open, ctx->n_fds)) {
    err_save = errno;
    free(cpus_to_open);
    msr_sys_destroy(ctx);
    free(topo);
    errno = err_save;
    return NULL;
  }
  free(cpus_to_open);
  free(topo);
  *n_pkg = ctx->n_pkg;
  *n_die = ctx->n_die;
  return ctx;
}

int msr_sys_destroy(raplcap_msr_sys_ctx* ctx) {
  assert(ctx);
  uint32_t i;
  int err_save = 0;
  for (i = 0; ctx->fds != NULL && i < ctx->n_fds; i++) {
    raplcap_log(DEBUG, "msr_sys_destroy: i=%"PRIu32", fd=%d\n", i, ctx->fds[i]);
    if (ctx->fds[i] > 0 && close(ctx->fds[i])) {
      err_save = errno;
      raplcap_perror(ERROR, "msr_sys_destroy: close");
    }
  }
  free(ctx->fds);
  free(ctx);
  errno = err_save;
  return err_save ? -1 : 0;
}

int msr_sys_read(const raplcap_msr_sys_ctx* ctx, uint64_t* msrval, uint32_t pkg, uint32_t die, off_t msr) {
  assert(ctx);
  assert(msr >= 0);
  assert(msrval != NULL);
  assert((pkg * ctx->n_die) + die < ctx->n_fds);
  if (pread(ctx->fds[(pkg * ctx->n_die) + die], msrval, sizeof(uint64_t), msr) == sizeof(uint64_t)) {
    raplcap_log(DEBUG, "msr_sys_read: msr=0x%lX, msrval=0x%016lX\n", msr, *msrval);
    return 0;
  }
  raplcap_log(DEBUG, "msr_sys_read(0x%lX): pread: %s\n", msr, strerror(errno));
  return -1;
}

int msr_sys_write(const raplcap_msr_sys_ctx* ctx, uint64_t msrval, uint32_t pkg, uint32_t die, off_t msr) {
  assert(ctx);
  assert(msr >= 0);
  assert((pkg * ctx->n_die) + die < ctx->n_fds);
  raplcap_log(DEBUG, "msr_sys_write: msr=0x%lX, msrval=0x%016lX\n", msr, msrval);
  if (pwrite(ctx->fds[(pkg * ctx->n_die) + die], &msrval, sizeof(uint64_t), msr) == sizeof(uint64_t)) {
    return 0;
  }
  raplcap_log(DEBUG, "msr_sys_write(0x%lX): pwrite: %s\n", msr, strerror(errno));
  return -1;
}
