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

static int get_cpu_to_socket_mapping(uint32_t* cpu_to_socket, uint32_t ncpus) {
  // assumes cpus are numbered from 0 to ncpus-1
  assert(cpu_to_socket != NULL);
  assert(ncpus > 0);
  uint32_t i;
  for (i = 0; i < ncpus; i++) {
    if (get_physical_package_id(i, &cpu_to_socket[i]) < 0) {
      return -1;
    }
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

int msr_get_num_pkgs(uint32_t *n_sockets) {
  uint32_t* cpu_to_socket;
  uint32_t ncpus;
  if ((ncpus = get_cpu_count()) == 0) {
    raplcap_perror(ERROR, "msr_get_num_pkgs: get_cpu_count");
    return -1;
  }
  if ((cpu_to_socket = malloc(ncpus * sizeof(uint32_t))) == NULL) {
    raplcap_perror(ERROR, "msr_get_num_pkgs: malloc");
    return -1;
  }
  if (get_cpu_to_socket_mapping(cpu_to_socket, ncpus)) {
    free(cpu_to_socket);
    return -1;
  }
  *n_sockets = count_unique_u32(cpu_to_socket, ncpus, NULL);
  raplcap_log(DEBUG, "msr_get_num_pkgs: ncpus=%"PRIu32", n_sockets=%"PRIu32"\n", ncpus, *n_sockets);
  free(cpu_to_socket);
  return 0;
}

raplcap_msr_sys_ctx* msr_sys_init(uint32_t* n_sockets) {
  assert(n_sockets);
  uint32_t* cpu_to_socket;
  raplcap_msr_sys_ctx* ctx = NULL;
  uint32_t ncpus;
  int err_save;
  // need to map CPU IDs to sockets
  if ((ncpus = get_cpu_count()) == 0) {
    raplcap_perror(ERROR, "msr_sys_init: get_cpu_count");
    return NULL;
  }
  // second half of the buffer is for duplicating/sorting socket mappings in count_unique_u32(...)
  if ((cpu_to_socket = malloc(2 * ncpus * sizeof(uint32_t))) == NULL ||
      (ctx = malloc(sizeof(*ctx))) == NULL) {
    raplcap_perror(ERROR, "msr_sys_init: malloc");
    goto init_fail;
  }
  if (get_cpu_to_socket_mapping(cpu_to_socket, ncpus)) {
    goto init_fail;
  }
  ctx->n_fds = count_unique_u32(cpu_to_socket, ncpus, (cpu_to_socket + ncpus));
  raplcap_log(DEBUG, "msr_sys_init: ncpus=%"PRIu32", sockets=%"PRIu32"\n", ncpus, ctx->n_fds);
  if ((ctx->fds = calloc(ctx->n_fds, sizeof(int))) == NULL) {
    raplcap_perror(ERROR, "msr_sys_init: calloc");
    goto init_fail;
  }
  // map cpu IDs to socket indexes; physical socket IDs may not be in range [0, nsockets), so we enforce this
  normalize_to_indexes(cpu_to_socket, ncpus, ctx->n_fds);
  raplcap_log(DEBUG, "msr_sys_init: normalized physical socket IDs to indexes, opening MSRs...\n");
  if (open_msrs(ctx->fds, ctx->n_fds, cpu_to_socket, ncpus)) {
    err_save = errno;
    msr_sys_destroy(ctx);
    free(cpu_to_socket);
    errno = err_save;
    return NULL;
  }
  free(cpu_to_socket);
  *n_sockets = ctx->n_fds;
  return ctx;
init_fail:
  free(ctx);
  free(cpu_to_socket);
  *n_sockets = 0;
  return NULL;
}

int msr_sys_destroy(raplcap_msr_sys_ctx* ctx) {
  assert(ctx);
  uint32_t i;
  int err_save = 0;
  for (i = 0; ctx->fds != NULL && i < ctx->n_fds; i++) {
    raplcap_log(DEBUG, "msr_sys_destroy: socket=%"PRIu32", fd=%d\n", i, ctx->fds[i]);
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

int msr_sys_read(const raplcap_msr_sys_ctx* ctx, uint64_t* msrval, uint32_t pkg, off_t msr) {
  assert(ctx);
  assert(msr >= 0);
  assert(msrval != NULL);
  if (pread(ctx->fds[pkg], msrval, sizeof(uint64_t), msr) == sizeof(uint64_t)) {
    raplcap_log(DEBUG, "msr_sys_read: msr=0x%lX, msrval=0x%016lX\n", msr, *msrval);
    return 0;
  }
  raplcap_log(DEBUG, "msr_sys_read(0x%lX): pread: %s\n", msr, strerror(errno));
  return -1;
}

int msr_sys_write(const raplcap_msr_sys_ctx* ctx, uint64_t msrval, uint32_t pkg, off_t msr) {
  assert(ctx);
  assert(msr >= 0);
  raplcap_log(DEBUG, "msr_sys_write: msr=0x%lX, msrval=0x%016lX\n", msr, msrval);
  if (pwrite(ctx->fds[pkg], &msrval, sizeof(uint64_t), msr) == sizeof(uint64_t)) {
    return 0;
  }
  raplcap_log(DEBUG, "msr_sys_write(0x%lX): pwrite: %s\n", msr, strerror(errno));
  return -1;
}
