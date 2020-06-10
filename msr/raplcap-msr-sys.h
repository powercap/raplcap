/**
 * Interface for MSR access.
 *
 * @author Connor Imes
 * @date 2020-06-09
 */
#ifndef _RAPLCAP_MSR_SYS_H_
#define _RAPLCAP_MSR_SYS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <sys/types.h>

#pragma GCC visibility push(hidden)

typedef struct raplcap_msr_sys_ctx raplcap_msr_sys_ctx;

int msr_get_num_pkg_die(const raplcap_msr_sys_ctx* ctx, uint32_t *n_pkg, uint32_t* n_die);

raplcap_msr_sys_ctx* msr_sys_init(uint32_t* n_pkg, uint32_t* n_die);

int msr_sys_destroy(raplcap_msr_sys_ctx* ctx);

int msr_sys_read(const raplcap_msr_sys_ctx* ctx, uint64_t* msrval, uint32_t pkg, uint32_t die, off_t msr);

int msr_sys_write(const raplcap_msr_sys_ctx* ctx, uint64_t msrval, uint32_t pkg, uint32_t die, off_t msr);

#pragma GCC visibility pop

#ifdef __cplusplus
}
#endif

#endif
